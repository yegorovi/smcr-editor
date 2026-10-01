#include "modfile.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSet>
#include <QStringConverter>

#include <algorithm>

namespace {

// старый дебил-дамп, лень удалять. надо покопать строку: переключить на #if 1 и смотреть хексом,
// зачем это тут было, уже не помню
#if 0
static QString hexDump(const QString& s, int maxChars) {
	QString out;
	for (int i = 0; i < s.size() && i < maxChars; ++i)
		out += QString::number(s.at(i).unicode(), 16).rightJustified(4, '0') + ' ';
	return out.trimmed(); // хуйня, но рабочая хуйня
}
#endif

struct Block {
	int start = 0; // начало строки, содержащей "class" (без перевода строки предыдущей строки)
	int open = 0;  // индекс '{'
	int close = 0; // индекс '}'
	int end = 0;   // индекс после '}' + ';' + перевода строки
	QString name;
};

struct Edit {
	int pos = 0;
	int len = 0;
	QString text;
};

struct Parsed {
	QString config;

	int unitsOpen = -1;
	int unitsClose = -1;
	QStringList units;

	int shadersOpen = -1;
	int shadersClose = -1;
	QVector<Block> shaderBlocks;

	int setsOpen = -1;
	int setsClose = -1;
	QVector<Block> setBlocks;

	int vehOpen = -1;
	int vehClose = -1;
	QVector<Block> vehBlocks;

	QHash<QString, QString> shaderSample;
	QHash<QString, QStringList> setShaders;
	QHash<QString, QStringList> cassetteSets; // класс кассеты -> его smcrSoundSets
	QVector<Cassette> cassettes;

	QStringList csvHeader;
	QVector<QStringList> csvRows;
	QHash<QString, int> csvIndex;
};

// ---------------------------------------------------------------- утилиты ---

QString readFileUtf8(const QString& path, QString* err) {
	QFile f(path);
	if (!f.open(QIODevice::ReadOnly)) {
		if (err) *err = QStringLiteral("Не удалось открыть %1").arg(path);
		return QString();
	}
	const QByteArray raw = f.readAll();
	f.close();
	QStringDecoder dec(QStringDecoder::Utf8, QStringDecoder::Flag::Stateless);
	const QString s = dec(raw);
	if (dec.hasError()) {
		if (err) *err = QStringLiteral("Файл не в UTF-8: %1").arg(path);
		return QString();
	}
	return s;
}

bool writeFileUtf8(const QString& path, const QString& text, QString* err) {
	QFile f(path);
	if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
		if (err) *err = QStringLiteral("Не удалось записать %1").arg(path);
		return false;
	}
	const QByteArray raw = text.toUtf8();
	const bool ok = (f.write(raw) == raw.size());
	f.close();
	if (!ok && err) *err = QStringLiteral("Запись оборвалась: %1").arg(path);
	return ok;
}

// бэкап делаем один раз и не трогаем больше, если сам отредактируешь и хер с тобой)
void makeBackup(const QString& path) {
	if (QFile::exists(path + QStringLiteral(".bak")))
		return;
	QFile::copy(path, path + QStringLiteral(".bak"));
}

int skipStringLit(const QString& t, int i) {
	const int n = t.size();
	++i;
	while (i < n) {
		if (t.at(i) == '"') {
			++i;
			break;
		}
		++i;
	}
	return i;
}

QString stringLit(const QString& t, int quotePos) {
	QString out;
	const int n = t.size();
	int i = quotePos + 1;
	while (i < n && t.at(i) != '"') {
		out += t.at(i);
		++i;
	}
	return out;
}

// скобки считаю руками, regex тут не живёт: скобки бывают внутри строк,
// а строки внутри скобок. config.cpp это ебейший бардак, делать нечего
int braceMatch(const QString& t, int open) {
	const int n = t.size();
	int depth = 0;
	for (int i = open; i < n; ++i) {
		const QChar c = t.at(i);
		if (c == '"') {
			i = skipStringLit(t, i);
			if (i >= n) return -1;
			--i;
			continue;
		}
		if (c == '{') {
			++depth;
		} else if (c == '}') {
			if (--depth == 0) return i;
		}
	}
	return -1;
}

int lineStart(const QString& t, int pos) {
	int i = pos;
	while (i > 0) {
		const QChar c = t.at(i - 1);
		if (c == '\t' || c == ' ') --i;
		else break;
	}
	return i;
}

int blockEnd(const QString& t, int closeBrace) {
	int e = closeBrace + 1;
	const int n = t.size();
	if (e < n && t.at(e) == ';') ++e;
	if (e < n && t.at(e) == '\r') ++e;
	if (e < n && t.at(e) == '\n') ++e;
	return e;
}

QVector<Block> childClasses(const QString& t, int from, int to) {
	QVector<Block> res;
	const int n = t.size();
	int i = from;
	while (i < to && i < n) {
		const QChar c = t.at(i);
		if (c == '"') {
			i = skipStringLit(t, i);
			continue;
		}
		if (c == '{') {
			const int e = braceMatch(t, i);
			i = (e < 0) ? i + 1 : e + 1;
			continue;
		}
		if (c == '/' && i + 1 < n && t.at(i + 1) == '/') {
			while (i < to && t.at(i) != '\n') ++i;
			continue;
		}
		if (c == '/' && i + 1 < n && t.at(i + 1) == '*') {
			const int e = t.indexOf(QStringLiteral("*/"), i + 2);
			i = (e < 0 || e >= to) ? to : e + 2;
			continue;
		}
		if (i + 5 <= to && t.mid(i, 5) == QLatin1String("class")
			&& (i == 0 || (!t.at(i - 1).isLetterOrNumber() && t.at(i - 1) != '_'))) {
			int j = i + 5;
			while (j < to && t.at(j).isSpace()) ++j;
			const int nameStart = j;
			while (j < to && (t.at(j).isLetterOrNumber() || t.at(j) == '_')) ++j;
			const QString name = t.mid(nameStart, j - nameStart);
			int p = j;
			while (p < to && t.at(p).isSpace()) ++p;
			if (p < to && t.at(p) == ':') {
				++p;
				while (p < to && t.at(p) != '{' && t.at(p) != ';' && t.at(p) != '}') ++p;
			}
			while (p < to && t.at(p).isSpace()) ++p;
			if (p >= to || t.at(p) != '{') {
				i = j;
				continue;
			}
			const int cb = braceMatch(t, p);
			if (cb < 0 || cb >= to) break;
			Block b;
			b.name = name;
			b.start = lineStart(t, i);
			b.open = p;
			b.close = cb;
			b.end = blockEnd(t, cb);
			res.append(b);
			i = cb + 1;
			continue;
		}
		++i;
	}
	return res;
}

const Block* findChild(const QVector<Block>& v, const QString& name) {
	for (const Block& b : v)
		if (b.name == name) return &b;
	return nullptr;
}

int findKey(const QString& t, int from, int to, const QString& key) {
	if (from < 0 || to <= from || to > t.size()) return -1;
	const QString sub = t.mid(from, to - from);
	const QRegularExpression rx(QStringLiteral("\\b") + QRegularExpression::escape(key) + QStringLiteral("\\s*(\\[\\])?\\s*="));
	const QRegularExpressionMatch m = rx.match(sub);
	if (!m.hasMatch()) return -1;
	return from + m.capturedEnd();
}

int afterValue(const QString& t, int pos, int to) {
	while (pos < to && t.at(pos).isSpace()) ++pos;
	return pos;
}

QString getStringValue(const QString& t, int from, int to, const QString& key) {
	int p = findKey(t, from, to, key);
	if (p < 0) return QString();
	p = afterValue(t, p, to);
	if (p >= to || t.at(p) != '"') return QString();
	return stringLit(t, p);
}

QStringList parseStrArrayAt(const QString& t, int braceOpen, int limit) {
	QStringList out;
	if (braceOpen < 0 || braceOpen >= limit) return out;
	const int close = braceMatch(t, braceOpen);
	if (close < 0 || close > limit) return out;
	const int n = t.size();
	int i = braceOpen + 1;
	while (i < close && i < n) {
		if (t.at(i) == '"') {
			out.append(stringLit(t, i));
			i = skipStringLit(t, i);
			continue;
		}
		++i;
	}
	return out;
}

QStringList getStrArray(const QString& t, int from, int to, const QString& key) {
	int p = findKey(t, from, to, key);
	if (p < 0) return QStringList();
	p = afterValue(t, p, to);
	if (p >= to || t.at(p) != '{') return QStringList();
	return parseStrArrayAt(t, p, to);
}

QVector<int> getIntArray(const QString& t, int from, int to, const QString& key) {
	QVector<int> out;
	int p = findKey(t, from, to, key);
	if (p < 0) return out;
	p = afterValue(t, p, to);
	if (p >= to || t.at(p) != '{') return out;
	const int close = braceMatch(t, p);
	if (close < 0 || close > to) return out;
	static const QRegularExpression num(QStringLiteral("-?\\d+"));
	const QString sub = t.mid(p + 1, close - p - 1);
	QRegularExpressionMatchIterator it = num.globalMatch(sub);
	while (it.hasNext())
			out.append(it.next().captured(0).toInt());
	return out;
}

// samples[] = {{"path", 1}} -> "path"
QString firstSamplePath(const QString& t, int from, int to) {
	int p = findKey(t, from, to, QStringLiteral("samples"));
	if (p < 0) return QString();
	p = afterValue(t, p, to);
	if (p >= to || t.at(p) != '{') return QString();
	const int close = braceMatch(t, p);
	if (close < 0) return QString();
	int i = p + 1;
	while (i < close) {
		if (t.at(i) == '{') {
			const int innerClose = braceMatch(t, i);
			if (innerClose < 0) return QString();
			int q = i + 1;
			while (q < innerClose && t.at(q) != '"') ++q;
			if (q < innerClose) return stringLit(t, q);
			return QString();
		}
		++i;
	}
	return QString();
}

// википедия на казахском, вставил сюда потому что мог. CSV бөлінген мәтіндік файл,
// онда деректер кесте түрінде сақталады, сол кездегі адамдар ойлап тапқан
QStringList splitCsvLine(const QString& line) {
	// QStringList r = line.split(','); // наивный сплит, рвал на кавычках с запятой внутри
	QStringList out;
	QString kucha;
	bool inQuotes = false;
	for (int i = 0; i < line.size(); ++i) {
		const QChar c = line.at(i);
		if (inQuotes) {
			if (c == '"') {
				if (i + 1 < line.size() && line.at(i + 1) == '"') {
					kucha += '"';
					++i;
				} else {
					inQuotes = false;
				}
			} else {
				kucha += c;
			}
		} else if (c == '"') {
			inQuotes = true;
		} else if (c == ',') {
			out.append(kucha);
			kucha.clear();
		} else {
			kucha += c;
		}
	}
	out.append(kucha);
	return out;
}

QString joinCsvLine(const QStringList& fields) {
	QString out;
	for (int i = 0; i < fields.size(); ++i) {
		if (i) out += ',';
		const QString v = fields.at(i);
		if (v.isEmpty()) continue;
		QString q = v;
		q.replace(QLatin1Char('"'), QStringLiteral("\"\""));
		out += QLatin1Char('"') + q + QLatin1Char('"');
	}
	return out;
}

// stringtable: 16 колонок, живых три, остальные под языки которых тут нет,
// удалять их нельзя, игра плачется
void parseCsv(const QString& csv, Parsed& p) {
	p.csvHeader.clear();
	p.csvRows.clear();
	p.csvIndex.clear();
	const QStringList lines = csv.split(QRegularExpression(QStringLiteral("\r\n|\n|\r")), Qt::SkipEmptyParts);
	for (int i = 0; i < lines.size(); ++i) {
		const QStringList fields = splitCsvLine(lines.at(i));
		if (fields.isEmpty()) continue;
		if (p.csvHeader.isEmpty()) {
			p.csvHeader = fields;
			continue;
		}
		const int idx = p.csvRows.size();
		p.csvRows.append(fields);
		p.csvIndex.insert(fields.first(), idx);
	}
}

QString csvGetValue(const Parsed& p, const QString& key, int col) {
	const int idx = p.csvIndex.value(key, -1);
	if (idx < 0) return QString();
	const QStringList& row = p.csvRows.at(idx);
	if (col < 0 || col >= row.size()) return QString();
	return row.at(col);
}

bool parseConfig(const QString& config, Parsed& p, QString* err) {
	p.config = config;

	const QVector<Block> top = childClasses(config, 0, config.size());
	const Block* patches = findChild(top, QStringLiteral("CfgPatches"));
	const Block* shaders = findChild(top, QStringLiteral("CfgSoundShaders"));
	const Block* sets = findChild(top, QStringLiteral("CfgSoundSets"));
	const Block* veh = findChild(top, QStringLiteral("CfgVehicles"));

	if (!shaders || !sets || !veh) {
		if (err) *err = QStringLiteral("В config.cpp не найдены CfgSoundShaders / CfgSoundSets / CfgVehicles");
		return false;
	}

	p.shadersOpen = shaders->open;
	p.shadersClose = shaders->close;
	p.shaderBlocks = childClasses(config, shaders->open + 1, shaders->close);

	p.setsOpen = sets->open;
	p.setsClose = sets->close;
	p.setBlocks = childClasses(config, sets->open + 1, sets->close);

	p.vehOpen = veh->open;
	p.vehClose = veh->close;
	p.vehBlocks = childClasses(config, veh->open + 1, veh->close);

	if (patches) {
		const QVector<Block> patchChildren = childClasses(config, patches->open + 1, patches->close);
		for (const Block& mod : patchChildren) {
			int k = findKey(config, mod.open + 1, mod.close, QStringLiteral("units"));
			if (k < 0) continue;
			k = afterValue(config, k, mod.close);
			if (k < mod.close && config.at(k) == '{') {
				const int close = braceMatch(config, k);
				if (close >= 0 && close < mod.close) {
					p.unitsOpen = k;
					p.unitsClose = close;
					p.units = parseStrArrayAt(config, k, mod.close);
				}
			}
		}
	}

	for (const Block& b : p.shaderBlocks)
		p.shaderSample.insert(b.name, firstSamplePath(config, b.open + 1, b.close));

	for (const Block& b : p.setBlocks)
		p.setShaders.insert(b.name, getStrArray(config, b.open + 1, b.close, QStringLiteral("soundShaders")));

	for (const Block& b : p.vehBlocks) {
		if (!b.name.startsWith(QStringLiteral("SMCR_Cassette_"))) continue;
		if (b.name == QStringLiteral("SMCR_Cassette_Base")) continue;
		Cassette c;
		c.key = b.name.mid(QStringLiteral("SMCR_Cassette_").size());
		c.present = true;
		c.album = getStringValue(config, b.open + 1, b.close, QStringLiteral("smcrAlbum"));
		const QStringList sets = getStrArray(config, b.open + 1, b.close, QStringLiteral("smcrSoundSets"));
		const QStringList titles = getStrArray(config, b.open + 1, b.close, QStringLiteral("smcrTitles"));
		const QVector<int> durs = getIntArray(config, b.open + 1, b.close, QStringLiteral("smcrDurations"));
		p.cassetteSets.insert(b.name, sets);
		for (int i = 0; i < sets.size(); ++i) {
			Track tr;
			tr.title = (i < titles.size()) ? titles.at(i) : QString();
			tr.duration = (i < durs.size()) ? durs.at(i) : 0;
			const QStringList sh = p.setShaders.value(sets.at(i));
			if (!sh.isEmpty())
				tr.sample = p.shaderSample.value(sh.first());
			c.tracks.append(tr);
		}
		p.cassettes.append(c);
	}
	return true;
}

// ЭТУ ХУЙНЮ НЕ ПРАВИТЬ, СЛОМАЕШЬ БЛЯТЬ. правки применяю одним проходом и по возрастанию позиции,
// иначе они съедают друг друга. два часа ловил это в первый раз
bool applyEdits(QString& text, QVector<Edit> edits, QString* err) {
	// for (int i = edits.size() - 1; i >= 0; --i) // обратный обход пробовал, позиции всё равно едут
	// 	text = text.left(edits[i].pos) + edits[i].text + text.mid(edits[i].pos + edits[i].len + 1);
	std::sort(edits.begin(), edits.end(), [](const Edit& a, const Edit& b) {
		if (a.pos != b.pos) return a.pos < b.pos;
		return a.len < b.len;
	});
	for (int i = 1; i < edits.size(); ++i) {
		const Edit& prev = edits.at(i - 1);
		const Edit& cur = edits.at(i);
		if (prev.pos + prev.len > cur.pos) {
			if (err) *err = QStringLiteral("Внутренняя ошибка: пересечение правок в config.cpp");
			return false;
		}
	}
	QString out;
	out.reserve(text.size() + 4096);
	int kursorka = 0;
	for (const Edit& e : edits) {
		if (e.pos < kursorka) {
			if (err) *err = QStringLiteral("Внутренняя ошибка: порядок правок");
			return false;
		}
		out += text.mid(kursorka, e.pos - kursorka);
		out += e.text;
		kursorka = e.pos + e.len;
	}
	out += text.mid(kursorka);
	text = out;
	return true;
}

QString quotedList(const QStringList& items) {
	QString out;
	for (int i = 0; i < items.size(); ++i) {
		if (i) out += QStringLiteral(", ");
		out += QLatin1Char('"') + items.at(i) + QLatin1Char('"');
	}
	return out;
}

QString intList(const QVector<int>& items) {
	QStringList parts;
	parts.reserve(items.size());
	for (int v : items)
		parts.append(QString::number(v));
	return parts.join(QStringLiteral(", "));
}

void addDelete(QVector<Edit>& edits, const Block& b) {
	edits.append(Edit{ b.start, b.end - b.start, QString() });
}

int insertPosForSection(const QString& config, int sectionCloseBrace, const QVector<int>& deletedStarts) {
	if (!deletedStarts.isEmpty())
		return *std::min_element(deletedStarts.begin(), deletedStarts.end());
	return lineStart(config, sectionCloseBrace);
}

QString shaderBlockText(const QString& key, int index, const QString& sample) {
	return QStringLiteral("\tclass SMCR_") + key + QLatin1Char('_') + QString::number(index)
		+ QStringLiteral("_Shader: SMCR_Shader_Base\r\n")
		+ QStringLiteral("\t{\r\n\t\tsamples[] = {{\"") + sample + QStringLiteral("\", 1}};\r\n\t};\r\n");
}

QString soundSetBlockText(const QString& key, int index) {
	return QStringLiteral("\tclass SMCR_") + key + QLatin1Char('_') + QString::number(index)
		+ QStringLiteral("_SoundSet: SMCR_SoundSet_Base\r\n")
		+ QStringLiteral("\t{\r\n\t\tsoundShaders[] = {\"SMCR_") + key + QLatin1Char('_')
		+ QString::number(index) + QStringLiteral("_Shader\"};\r\n\t};\r\n");
}

QString cassetteBlockText(const Cassette& c) {
	QStringList titles;
	QVector<int> durs;
	QStringList sets;
	for (int i = 0; i < c.tracks.size(); ++i) {
		titles.append(c.tracks.at(i).title.isEmpty()
			? (QStringLiteral("Трек ") + QString::number(i + 1))
			: c.tracks.at(i).title);
		durs.append(qMax(1, c.tracks.at(i).duration));
		sets.append(QStringLiteral("SMCR_") + c.key + QLatin1Char('_') + QString::number(i + 1) + QStringLiteral("_SoundSet"));
	}
	return QStringLiteral("\tclass ") + c.className() + QStringLiteral(": SMCR_Cassette_Base\r\n")
		+ QStringLiteral("\t{\r\n")
		+ QStringLiteral("\t\tscope = 2;\r\n")
		+ QStringLiteral("\t\tdisplayName = \"") + c.strNameDollar() + QStringLiteral("\";\r\n")
		+ QStringLiteral("\t\tdescriptionShort = \"") + c.strDescDollar() + QStringLiteral("\";\r\n")
		+ QStringLiteral("\t\tsmcrAlbum = \"") + c.album + QStringLiteral("\";\r\n")
		+ QStringLiteral("\t\tsmcrTitles[] = {") + quotedList(titles) + QStringLiteral("};\r\n")
		+ QStringLiteral("\t\tsmcrSoundSets[] = {") + quotedList(sets) + QStringLiteral("};\r\n")
		+ QStringLiteral("\t\tsmcrDurations[] = {") + intList(durs) + QStringLiteral("};\r\n")
		+ QStringLiteral("\t};\r\n");
}

QString csvRowFor(const QString& key, const QString& value, int totalCols, int lastCol) {
	QStringList row;
	row.reserve(totalCols);
	for (int i = 0; i < totalCols; ++i)
		row.append(i == 0 ? key : (i <= lastCol ? value : QString()));
	return joinCsvLine(row);
}

int lastCassetteInsertPos(const QString& config, const Parsed& p) {
	int pos = -1;
	for (const Block& b : p.vehBlocks)
		if (b.name.startsWith(QStringLiteral("SMCR_Cassette_")))
			pos = b.end;
	if (pos >= 0) return pos;
	return lineStart(config, p.vehClose);
}

QSet<QString> doomedShaderNames(const Parsed& p, const QString& key) {
	QSet<QString> out;
	const QString prefix = QStringLiteral("SMCR_") + key + QStringLiteral("_");
	for (const Block& b : p.shaderBlocks)
		if (b.name.startsWith(prefix) && b.name.endsWith(QStringLiteral("_Shader")))
			out.insert(b.name);
	const QString cls = QStringLiteral("SMCR_Cassette_") + key;
	const QStringList sets = p.cassetteSets.value(cls);
	for (const QString& s : sets) {
		const QStringList sh = p.setShaders.value(s);
		for (const QString& x : sh)
			out.insert(x);
	}
	return out;
}

QSet<QString> doomedSoundSetNames(const Parsed& p, const QString& key) {
	QSet<QString> out;
	const QString prefix = QStringLiteral("SMCR_") + key + QStringLiteral("_");
	for (const Block& b : p.setBlocks)
		if (b.name.startsWith(prefix) && b.name.endsWith(QStringLiteral("_SoundSet")))
			out.insert(b.name);
	const QString cls = QStringLiteral("SMCR_Cassette_") + key;
	for (const QString& s : p.cassetteSets.value(cls))
		out.insert(s);
	return out;
}

// транслит кириллицы для имён файлов внутри PBO: символ нижнего регистра ->
// латиница, nullptr = не кириллица. пустая строка = знак исчезает (ъ, ь)
const char* latFor(QChar ch) {
	switch (ch.toLower().unicode()) {
	case 0x0430: return "a";
	case 0x0431: return "b";
	case 0x0432: return "v";
	case 0x0433: return "g";
	case 0x0434: return "d";
	case 0x0435: return "e";
	case 0x0451: return "e";
	case 0x0436: return "zh";
	case 0x0437: return "z";
	case 0x0438: return "i";
	case 0x0439: return "y";
	case 0x043A: return "k";
	case 0x043B: return "l";
	case 0x043C: return "m";
	case 0x043D: return "n";
	case 0x043E: return "o";
	case 0x043F: return "p";
	case 0x0440: return "r";
	case 0x0441: return "s";
	case 0x0442: return "t";
	case 0x0443: return "u";
	case 0x0444: return "f";
	case 0x0445: return "h";
	case 0x0446: return "c";
	case 0x0447: return "ch";
	case 0x0448: return "sh";
	case 0x0449: return "sch";
	case 0x044A: return "";
	case 0x044B: return "y";
	case 0x044C: return "";
	case 0x044D: return "e";
	case 0x044E: return "yu";
	case 0x044F: return "ya";
	case 0x0454: return "ye";
	case 0x0456: return "i";
	case 0x0457: return "yi";
	case 0x0490: return "g";
	case 0x045E: return "u";
	default: return nullptr;
	}
}

// файл лежит внутри папки мода (в т.ч. в подпапках)
bool insideRoot(const QString& root, const QString& file) {
	QString r = QDir(root).absolutePath();
	r.replace(QLatin1Char('\\'), QLatin1Char('/'));
	QString p = QFileInfo(file).absolutePath();
	p.replace(QLatin1Char('\\'), QLatin1Char('/'));
	return p.compare(r, Qt::CaseInsensitive) == 0
		|| p.startsWith(r + QLatin1Char('/'), Qt::CaseInsensitive);
}

} // namespace

// ---------------------------------------------------------------------------

bool ModFile::load(const QString& root, QString* err) {
	m_root = root;
	m_cassettes.clear();
	m_warnings.clear();

	const QString cfgPath = QDir(root).filePath(QStringLiteral("config.cpp"));
	QString e;
	const QString config = readFileUtf8(cfgPath, &e);
	if (!e.isEmpty()) {
		if (err) *err = e;
		return false;
	}

	const QString prefixFile = QDir(root).filePath(QStringLiteral("$PREFIX$"));
	if (QFile::exists(prefixFile)) {
		const QString pref = readFileUtf8(prefixFile, nullptr).trimmed();
		if (!pref.isEmpty()) m_prefix = pref;
	} else {
		m_prefix = QFileInfo(root).fileName();
	}
	m_modName = m_prefix;

	Parsed p;
	if (!parseConfig(config, p, err))
		return false;

	const QString csvPath = QDir(root).filePath(QStringLiteral("stringtable.csv"));
	if (QFile::exists(csvPath))
		parseCsv(readFileUtf8(csvPath, nullptr), p);

	for (Cassette c : p.cassettes) {
		c.itemName = csvGetValue(p, c.strName(), 5);
		if (c.itemName.isEmpty()) c.itemName = csvGetValue(p, c.strName(), 1);
		const QString descKey = QStringLiteral("STR_SMCR_Cassette_") + c.key + QStringLiteral("_Desc");
		c.itemDesc = csvGetValue(p, descKey, 5);
		if (c.itemDesc.isEmpty()) c.itemDesc = csvGetValue(p, descKey, 1);

		int missing = 0;
		QString folder;
		for (Track& t : c.tracks) {
			t.sourcePath = absFromSample(t.sample);
			if (t.sourcePath.isEmpty() || !QFile::exists(t.sourcePath)) {
				t.sourcePath.clear();
				++missing;
			}
			if (folder.isEmpty() && !t.sample.isEmpty()) {
				QString s = t.sample;
				s.replace(QLatin1Char('\\'), QLatin1Char('/'));
				const QString head = m_prefix + QStringLiteral("/Sounds/");
				if (s.startsWith(head)) {
					const QString rest = s.mid(head.size());
					const int slash = rest.lastIndexOf(QLatin1Char('/'));
					folder = (slash >= 0) ? rest.left(slash) : QString();
				}
			}
		}
		c.soundFolder = folder;
		if (missing > 0)
			m_warnings.append(QStringLiteral("%1: нет ogg-файлов: %2").arg(c.className()).arg(missing));
		m_cassettes.append(c);
	}
	return true;
}

QString ModFile::configPath() const {
	return QDir(m_root).filePath(QStringLiteral("config.cpp"));
}

QString ModFile::stringTablePath() const {
	return QDir(m_root).filePath(QStringLiteral("stringtable.csv"));
}

QString ModFile::absFromSample(const QString& sample) const {
	if (sample.isEmpty()) return QString();
	QString s = sample;
	s.replace(QLatin1Char('\\'), QLatin1Char('/'));
	const QString head = m_prefix + QLatin1Char('/');
	QString p;
	if (s.startsWith(head))
		p = QDir(m_root).filePath(s.mid(head.size()));
	else
		p = QDir(m_root).filePath(s);
	// в config.cpp путь без расширения, .ogg дописываю тут
	if (QFileInfo(p).suffix().isEmpty())
		p += QStringLiteral(".ogg");
	return p;
}

QString ModFile::sampleFor(const QString& soundFolder, const QString& oggPath) const {
	QString s = m_prefix + QStringLiteral("\\Sounds");
	if (!soundFolder.isEmpty()) {
		QString f = soundFolder;
		f.replace(QLatin1Char('/'), QLatin1Char('\\'));
		s += QLatin1Char('\\') + f;
	}
	s += QLatin1Char('\\') + baseNameNoExt(oggPath);
	return s;
}

QString ModFile::sanitizeKey(const QString& raw) {
	QString out;
	for (const QChar& ch : raw) {
		if (ch.isLetterOrNumber() || ch == '_')
			out += ch;
		else if (ch == ' ' || ch == '-' || ch == '.' || ch == '\t')
			out += QLatin1Char('_');
	}
	return out;
}

QString ModFile::translit(const QString& raw) {
	QString out;
	for (const QChar& ch : raw) {
		if (ch == QLatin1Char('_') || ch == QLatin1Char('-')) {
			out += ch;
			continue;
		}
		if (ch.isLetterOrNumber() && ch.unicode() < 128) {
			out += ch;
			continue;
		}
		const char* lat = latFor(ch);
		if (lat) {
			QString s = QString::fromLatin1(lat);
			if (ch.isUpper() && !s.isEmpty()) s[0] = s[0].toUpper();
			out += s;
		} else {
			out += QLatin1Char('_');
		}
	}
	return out;
}

QString ModFile::sanitizeFolder(const QString& raw) {
	QString s = raw.trimmed();
	s.replace(QLatin1Char('\\'), QLatin1Char('/'));
	QString out;
	for (const QString& part : s.split(QLatin1Char('/'))) {
		const QString seg = translit(part.trimmed());
		if (seg.isEmpty()) continue;
		if (!out.isEmpty()) out += QLatin1Char('/');
		out += seg;
	}
	return out;
}

QString ModFile::baseNameNoExt(const QString& oggPath) {
	return QFileInfo(oggPath).completeBaseName();
}

QString ModFile::defaultItemName(const QString& album) {
	return album.isEmpty() ? QString() : QStringLiteral("Кассета <") + album + QLatin1Char('>');
}

QString ModFile::defaultDesc() {
	return QStringLiteral("Вставьте в автомагнитолу любого автомобиля.");
}

bool ModFile::save(const Cassette& c, QString* err, QStringList* notes) {
	if (m_root.isEmpty()) {
		if (err) *err = QStringLiteral("Папка мода не задана");
		return false;
	}
	const QString key = sanitizeKey(c.key);
	if (key.isEmpty()) {
		if (err) *err = QStringLiteral("Укажите имя кассеты (латиницей)");
		return false;
	}
	if (c.tracks.isEmpty()) {
		if (err) *err = QStringLiteral("Добавьте хотя бы один ogg-файл");
		return false;
	}

	Cassette edited = c;
	edited.key = key;
	edited.album = edited.album.trimmed();
	if (edited.album.isEmpty()) edited.album = key;
	if (edited.itemName.isEmpty()) edited.itemName = defaultItemName(edited.album);
	if (edited.itemDesc.isEmpty()) edited.itemDesc = defaultDesc();

	QString osibka;
	const QString config = readFileUtf8(configPath(), &osibka);
	if (!osibka.isEmpty()) {
		if (err) *err = osibka;
		return false;
	}
	Parsed p;
	if (!parseConfig(config, p, err))
		return false;
	if (!edited.present) {
		for (const Cassette& x : p.cassettes) {
			if (x.key == key) {
				if (err) *err = QStringLiteral("%1 уже есть в config.cpp").arg(x.className());
				return false;
			}
		}
	}

	QString csv;
	if (QFile::exists(stringTablePath())) {
		csv = readFileUtf8(stringTablePath(), nullptr);
		parseCsv(csv, p);
	}

	// --- 1. ogg -> Sounds\<folder> -----------------------------------------
	edited.soundFolder = sanitizeFolder(edited.soundFolder);
	const QString targetDir = QDir(m_root).filePath(
		edited.soundFolder.isEmpty() ? QStringLiteral("Sounds")
		                              : QStringLiteral("Sounds/") + edited.soundFolder);
	if (!QDir().mkpath(targetDir)) {
		if (err) *err = QStringLiteral("Не удалось создать папку %1").arg(targetDir);
		return false;
	}
	QSet<QString> seen;
	for (int i = 0; i < edited.tracks.size(); ++i) {
		Track& t = edited.tracks[i];
		if (t.sourcePath.isEmpty() || !QFile::exists(t.sourcePath)) {
			if (err) *err = QStringLiteral("Трек %1: файл не найден").arg(i + 1);
			return false;
		}
		// имя файла в моде всегда латиницей: движок не открывает кириллицу в путях внутри PBO
		const QFileInfo srcInfo(t.sourcePath);
		const QString origName = srcInfo.fileName();
		QString base = translit(srcInfo.completeBaseName());
		if (base.isEmpty()) base = QStringLiteral("track_%1").arg(i + 1);
		QString ext = srcInfo.suffix();
		if (ext.isEmpty()) ext = QStringLiteral("ogg");
		const QString name = base + QLatin1Char('.') + ext;
		if (seen.contains(name.toLower())) {
			if (err) *err = QStringLiteral("Два файла с одинаковым именем: %1").arg(name);
			return false;
		}
		seen.insert(name.toLower());
		const QString src = srcInfo.absoluteFilePath();
		const QString dst = QFileInfo(targetDir + QLatin1Char('/') + name).absoluteFilePath();
		if (src.compare(dst, Qt::CaseInsensitive) != 0) {
			bool ok = true;
			if (QFile::exists(dst)) {
				if (QFileInfo(dst).size() != QFileInfo(src).size()) {
					QFile::remove(dst);
					ok = QFile::copy(src, dst);
				}
			} else {
				ok = QFile::copy(src, dst);
			}
			if (!ok) {
				if (err) *err = QStringLiteral("Не удалось скопировать %1").arg(name);
				return false;
			}
			if (notes && origName.compare(name, Qt::CaseInsensitive) != 0)
				notes->append(QStringLiteral("Транслит: %1 -> %2").arg(origName, name));
			// старое имя было внутри мода, новое уже лежит рядом, старое больше не нужно
			if (insideRoot(m_root, src) && QFile::exists(src)) {
				if (QFile::remove(src)) {
					if (notes)
						notes->append(QStringLiteral("Удалён старый файл: %1").arg(src));
				} else if (notes) {
					notes->append(QStringLiteral("Не вышло удалить старый файл (занят другим процессом?): %1").arg(src));
				}
			}
		}
		t.sourcePath = dst;
		t.sample = sampleFor(edited.soundFolder, dst);
		if (t.duration < 1) t.duration = 1;
		if (t.title.isEmpty()) t.title = srcInfo.completeBaseName();
	}

	// --- 2. config.cpp -----------------------------------------------------
	QVector<Edit> edits;

	{ // CfgSoundShaders
		QString text;
		QVector<int> dels;
		const QSet<QString> doomed = doomedShaderNames(p, key);
		for (const Block& b : p.shaderBlocks) {
			if (!doomed.contains(b.name)) continue;
			addDelete(edits, b);
			dels.append(b.start);
		}
		for (int i = 0; i < edited.tracks.size(); ++i)
			text += shaderBlockText(key, i + 1, edited.tracks.at(i).sample);
		Edit ins;
		ins.pos = insertPosForSection(config, p.shadersClose, dels);
		ins.text = text;
		edits.append(ins);
	}

	{ // CfgSoundSets
		QString text;
		QVector<int> dels;
		const QSet<QString> doomed = doomedSoundSetNames(p, key);
		for (const Block& b : p.setBlocks) {
			if (!doomed.contains(b.name)) continue;
			addDelete(edits, b);
			dels.append(b.start);
		}
		for (int i = 0; i < edited.tracks.size(); ++i)
			text += soundSetBlockText(key, i + 1);
		Edit ins;
		ins.pos = insertPosForSection(config, p.setsClose, dels);
		ins.text = text;
		edits.append(ins);
	}

	{ // CfgVehicles
		const QString text = cassetteBlockText(edited);
		const Block* existing = nullptr;
		for (const Block& b : p.vehBlocks)
			if (b.name == edited.className()) existing = &b;
		if (existing) {
			edits.append(Edit{ existing->start, existing->end - existing->start, text });
		} else {
			Edit ins;
			ins.pos = lastCassetteInsertPos(config, p);
			ins.text = text;
			edits.append(ins);
		}
	}

	// units[]
	if (!p.units.contains(edited.className()) && p.unitsOpen >= 0) {
		QStringList units = p.units;
		units.append(edited.className());
		edits.append(Edit{ p.unitsOpen + 1, p.unitsClose - p.unitsOpen - 1, quotedList(units) });
	}

	QString newConfig = config;
	if (!applyEdits(newConfig, edits, err))
		return false;

	// --- 3. stringtable.csv ------------------------------------------------
	QString newCsv = csv;
	if (!csv.isEmpty()) {
		QStringList lines = csv.split(QRegularExpression(QStringLiteral("\r\n|\n|\r")), Qt::SkipEmptyParts);
		int columns = p.csvHeader.size();
		if (columns < 6) columns = 16;
		int lastCol = columns - 1;
		if (!p.csvHeader.isEmpty()) {
			while (lastCol > 1 && (lastCol >= p.csvHeader.size() || p.csvHeader.at(lastCol).isEmpty()))
				--lastCol;
		}
		const QString nameKey = QStringLiteral("STR_SMCR_Cassette_") + key;
		const QString descKey = nameKey + QStringLiteral("_Desc");
		bool hasName = false, hasDesc = false;
		for (int i = 1; i < lines.size(); ++i) {
			QStringList f = splitCsvLine(lines.at(i));
			if (f.isEmpty()) continue;
			QString target;
			if (f.first() == nameKey) {
				target = edited.itemName;
				hasName = true;
			} else if (f.first() == descKey) {
				target = edited.itemDesc;
				hasDesc = true;
			} else {
				continue;
			}
			while (f.size() < columns) f.append(QString());
			for (int col = 1; col <= lastCol && col < f.size(); ++col)
				f[col] = target;
			while (f.size() > columns) f.removeLast();
			lines[i] = joinCsvLine(f);
		}
		QString add;
		if (!hasName) add += csvRowFor(nameKey, edited.itemName, columns, lastCol) + QStringLiteral("\r\n");
		if (!hasDesc) add += csvRowFor(descKey, edited.itemDesc, columns, lastCol) + QStringLiteral("\r\n");
		newCsv = lines.join(QStringLiteral("\r\n")) + QStringLiteral("\r\n") + add;
	}

	// --- 4. запись ---------------------------------------------------------
	// QFile f(configPath());
	// if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) return false; // файл уже занят, open врал
	// f.write(newConfig.toUtf8());
	// f.close(); // без close хвост терялся
	makeBackup(configPath());
	if (!writeFileUtf8(configPath(), newConfig, err))
		return false;
	if (!newCsv.isEmpty()) {
		makeBackup(stringTablePath());
		if (!writeFileUtf8(stringTablePath(), newCsv, err))
			return false;
	}
	return true;
}

bool ModFile::remove(const QString& key, QString* err) {
	QString e;
	const QString config = readFileUtf8(configPath(), &e);
	if (!e.isEmpty()) {
		if (err) *err = e;
		return false;
	}
	Parsed p;
	if (!parseConfig(config, p, err))
		return false;

	const QString cls = QStringLiteral("SMCR_Cassette_") + key;
	QVector<Edit> edits;
	const QSet<QString> shaders = doomedShaderNames(p, key);
	const QSet<QString> sets = doomedSoundSetNames(p, key);
	for (const Block& b : p.shaderBlocks)
		if (shaders.contains(b.name)) addDelete(edits, b);
	for (const Block& b : p.setBlocks)
		if (sets.contains(b.name)) addDelete(edits, b);
	for (const Block& b : p.vehBlocks)
		if (b.name == cls) addDelete(edits, b);

	if (edits.isEmpty()) {
		if (err) *err = QStringLiteral("%1 не найдена в config.cpp").arg(cls);
		return false;
	}
	if (p.units.contains(cls) && p.unitsOpen >= 0) {
		QStringList units = p.units;
		units.removeAll(cls);
		edits.append(Edit{ p.unitsOpen + 1, p.unitsClose - p.unitsOpen - 1, quotedList(units) });
	}

	QString newConfig = config;
	if (!applyEdits(newConfig, edits, err))
		return false;

	QString newCsv;
	if (QFile::exists(stringTablePath())) {
		const QString csv = readFileUtf8(stringTablePath(), nullptr);
		QStringList lines = csv.split(QRegularExpression(QStringLiteral("\r\n|\n|\r")), Qt::SkipEmptyParts);
		const QString nameKey = QStringLiteral("STR_SMCR_Cassette_") + key;
		const QString descKey = nameKey + QStringLiteral("_Desc");
		QStringList kept;
		for (int i = 0; i < lines.size(); ++i) {
			if (i == 0) {
				kept.append(lines.at(i));
				continue;
			}
			const QStringList f = splitCsvLine(lines.at(i));
			if (!f.isEmpty() && (f.first() == nameKey || f.first() == descKey))
				continue;
			kept.append(lines.at(i));
		}
		newCsv = kept.join(QStringLiteral("\r\n")) + QStringLiteral("\r\n");
	}

	makeBackup(configPath());
	if (!writeFileUtf8(configPath(), newConfig, err))
		return false;
	if (!newCsv.isEmpty()) {
		makeBackup(stringTablePath());
		if (!writeFileUtf8(stringTablePath(), newCsv, err))
			return false;
	}
	return true;
}
