#pragma once

#include <QHash>
#include <QString>
#include <QStringList>
#include <QVector>

struct Track {
	QString sourcePath; // абсолютный путь к ogg на диске (пустой если файла нет)
	QString sample;     // как лежит в config.cpp, расширения .ogg там нет
	QString title;
	int duration = 0;   // секунд
};

struct Cassette {
	QString key;          // "Kino" -> класс SMCR_Cassette_Kino
	QString album;        // smcrAlbum
	QString itemName;     // текст stringtable (поле original/english)
	QString itemDesc;     // текст stringtable _Desc
	QString soundFolder;  // подпапка внутри Sounds\ ("" = Sounds\)
	QVector<Track> tracks;
	bool present = false; // уже есть в config.cpp

	QString className() const { return QStringLiteral("SMCR_Cassette_") + key; }
	QString strName() const { return QStringLiteral("STR_SMCR_Cassette_") + key; }
	QString strNameDollar() const { return QStringLiteral("$STR_SMCR_Cassette_") + key; }
	QString strDescDollar() const { return QStringLiteral("$STR_SMCR_Cassette_") + key + QStringLiteral("_Desc"); }
};

class ModFile {
public:
	bool load(const QString& root, QString* err);

	QString root() const { return m_root; }
	QString prefix() const { return m_prefix; }
	QString modName() const { return m_modName; }
	const QVector<Cassette>& cassettes() const { return m_cassettes; }
	const QStringList& warnings() const { return m_warnings; }

	QString configPath() const;
	QString stringTablePath() const;

	// сохранение: кладёт ogg в Sounds\<folder>, дописывает config.cpp и stringtable.csv
	// notes — что переименовано/подчищено при транслите
	bool save(const Cassette& c, QString* err, QStringList* notes = nullptr);
	// удаление: вычищает кассету из config.cpp и stringtable.csv, ogg не трогает
	bool remove(const QString& key, QString* err);

	QString absFromSample(const QString& sample) const;
	QString sampleFor(const QString& soundFolder, const QString& oggPath) const;

	static QString sanitizeKey(const QString& raw);
	static QString sanitizeFolder(const QString& raw);
	// кириллица и прочая не-ASCII ерунда -> латиница; пробелы/скобки/точки -> '_'
	static QString translit(const QString& raw);
	static QString baseNameNoExt(const QString& oggPath);
	static QString defaultItemName(const QString& album);
	static QString defaultDesc();

protected:
	QString m_root;
	QString m_prefix = QStringLiteral("SM_Car_Radio");
	QString m_modName = QStringLiteral("SM_Car_Radio");
	QVector<Cassette> m_cassettes;
	QStringList m_warnings;
};
