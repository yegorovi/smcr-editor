#include "mainwindow.h"
#include "convert.h"
#include "ogg.h"

#include <QApplication>
#include <QAbstractItemView>
#include <QColor>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QMimeData>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSettings>
#include <QSplitter>
#include <QTableWidget>
#include <QVBoxLayout>

#include <algorithm>
#include <functional>

namespace {

// нигде не вызывается, оставил на будущее для строки "сколько осталось"
QString prettyDuration(int sec) {
	if (sec <= 0) return QStringLiteral("—");
	const int m = sec / 60;
	const int s = sec % 60;
	return QStringLiteral("%1:%2").arg(m).arg(s, 2, 10, QLatin1Char('0'));
}

} // namespace

MainWindow::MainWindow(QWidget* parent)
	: QMainWindow(parent) {
	setWindowTitle(QStringLiteral("SMCR Editor — SM_Car_Radio"));
	// 1180x780 под свой монитор
	// resize(1024, 700); // старый размер, глазом на глаз не проверял
	// if (frameGeometry().width() > screen()->size().width()) move(0, 0); // хуй знает что там с мультиэкраном
	resize(1180, 780);
	setAcceptDrops(true);
	// пробовал красиво без рамки, получилась хуйня но может пригодится
	// setWindowFlag(Qt::FramelessWindowHint, true);

	auto* central = new QWidget(this);
	auto* rootLay = new QVBoxLayout(central);
	rootLay->setContentsMargins(8, 8, 8, 8);
	rootLay->setSpacing(6);

	// --- строка папки мода -------------------------------------------------
	auto* topRow = new QHBoxLayout;
	m_rootEdit = new QLineEdit;
	m_rootEdit->setPlaceholderText(QStringLiteral("Папка распакованного мода, например V:\\dayz\\SM_Car_Radio"));
	auto* btnBrowse = new QPushButton(QStringLiteral("Обзор…"));
	auto* btnReload = new QPushButton(QStringLiteral("Перечитать"));
	topRow->addWidget(new QLabel(QStringLiteral("Папка мода:")));
	topRow->addWidget(m_rootEdit, 1);
	topRow->addWidget(btnBrowse);
	topRow->addWidget(btnReload);
	rootLay->addLayout(topRow);

	// --- сплиттер: список кассет | редактор --------------------------------
	auto* split = new QSplitter;
	rootLay->addWidget(split, 1);

	auto* left = new QWidget;
	auto* leftLay = new QVBoxLayout(left);
	leftLay->setContentsMargins(0, 0, 0, 0);
	leftLay->addWidget(new QLabel(QStringLiteral("Кассеты в моде:")));
	m_list = new QListWidget;
	m_list->setMinimumWidth(260);
	m_list->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
	m_list->setTextElideMode(Qt::ElideRight);
	leftLay->addWidget(m_list, 1);
	auto* btnNew = new QPushButton(QStringLiteral("＋ Новая кассета"));
	m_btnDelete = new QPushButton(QStringLiteral("Удалить кассету"));
	leftLay->addWidget(btnNew);
	leftLay->addWidget(m_btnDelete);
	split->addWidget(left);

	auto* right = new QWidget;
	auto* rightLay = new QVBoxLayout(right);
	rightLay->setContentsMargins(0, 0, 0, 0);

	// --- поля кассеты ------------------------------------------------------
	auto* box = new QGroupBox(QStringLiteral("Кассета"));
	auto* grid = new QGridLayout(box);
	m_keyEdit = new QLineEdit;
	m_albumEdit = new QLineEdit;
	m_itemNameEdit = new QLineEdit;
	m_descEdit = new QLineEdit;
	m_folderEdit = new QLineEdit;
	m_preview = new QLabel;
	m_preview->setWordWrap(true);
	m_preview->setTextInteractionFlags(Qt::TextSelectableByMouse);
	m_pathPreview = new QLabel;
	m_pathPreview->setWordWrap(true);

	grid->addWidget(new QLabel(QStringLiteral("Имя (латиницей)")), 0, 0);
	grid->addWidget(m_keyEdit, 0, 1, 1, 3);
	grid->addWidget(new QLabel(QStringLiteral("Название альбома")), 1, 0);
	grid->addWidget(m_albumEdit, 1, 1, 1, 3);
	grid->addWidget(new QLabel(QStringLiteral("Имя предмета")), 2, 0);
	grid->addWidget(m_itemNameEdit, 2, 1, 1, 3);
	grid->addWidget(new QLabel(QStringLiteral("Описание")), 3, 0);
	grid->addWidget(m_descEdit, 3, 1, 1, 3);
	grid->addWidget(new QLabel(QStringLiteral("Папка в Sounds")), 4, 0);
	grid->addWidget(m_folderEdit, 4, 1, 1, 3);
	grid->addWidget(new QLabel(), 5, 0);
	grid->addWidget(m_preview, 5, 1, 1, 3);
	grid->addWidget(m_pathPreview, 6, 1, 1, 3);
	grid->setColumnStretch(1, 1);
	rightLay->addWidget(box);

	// --- таблица треков ----------------------------------------------------
	auto* trackBox = new QGroupBox(QStringLiteral("Треки"));
	auto* trackLay = new QVBoxLayout(trackBox);
	m_table = new QTableWidget(0, 4);
	m_table->setHorizontalHeaderLabels({ QStringLiteral("№"), QStringLiteral("Файл ogg"),
		QStringLiteral("Название трека"), QStringLiteral("Сек") });
	m_table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
	m_table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
	m_table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
	m_table->horizontalHeader()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
	m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
	m_table->setSelectionMode(QAbstractItemView::ExtendedSelection);
	m_table->setAlternatingRowColors(true);
	m_table->verticalHeader()->setVisible(false);
	m_table->setEditTriggers(QAbstractItemView::DoubleClicked | QAbstractItemView::EditKeyPressed);
	trackLay->addWidget(m_table);

	auto* trackBtns = new QHBoxLayout;
	auto* btnAdd = new QPushButton(QStringLiteral("＋ ogg…"));
	auto* btnDelTrack = new QPushButton(QStringLiteral("− убрать"));
	auto* btnUp = new QPushButton(QStringLiteral("↑"));
	auto* btnDown = new QPushButton(QStringLiteral("↓"));
	auto* btnTitles = new QPushButton(QStringLiteral("Названия из файлов"));
	auto* btnDurs = new QPushButton(QStringLiteral("Длительности из ogg"));
	trackBtns->addWidget(btnAdd);
	trackBtns->addWidget(btnDelTrack);
	trackBtns->addWidget(btnUp);
	trackBtns->addWidget(btnDown);
	trackBtns->addSpacing(12);
	trackBtns->addWidget(btnTitles);
	trackBtns->addWidget(btnDurs);
	trackBtns->addStretch(1);
	trackLay->addLayout(trackBtns);
	rightLay->addWidget(trackBox, 1);

	// --- низ ---------------------------------------------------------------
	auto* bottom = new QHBoxLayout;
	m_btnSave = new QPushButton(QStringLiteral("Сохранить в мод"));
	m_btnSave->setMinimumHeight(32);
	auto* hint = new QLabel(QStringLiteral("ЛКМ по списку — выбрать кассету. ogg можно перетащить в таблицу."));
	hint->setWordWrap(true);
	bottom->addWidget(m_btnSave);
	bottom->addWidget(hint, 1);
	rightLay->addLayout(bottom);

	split->addWidget(right);
	split->setStretchFactor(0, 0);
	split->setStretchFactor(1, 1);
	split->setSizes({ 300, 860 });

	// --- лог ---------------------------------------------------------------
	// "наш паровоз вперед летит, он кочергу в зубах держит" это не про код, просто вставил сюда
	m_log = new QPlainTextEdit;
	m_log->setReadOnly(true);
	m_log->setMaximumHeight(120);
	m_log->setPlaceholderText(QStringLiteral("Здесь будут сообщения…"));
	rootLay->addWidget(m_log);

	setCentralWidget(central);

	// --- связи -------------------------------------------------------------
	connect(btnBrowse, &QPushButton::clicked, this, &MainWindow::browseRoot);
	connect(btnReload, &QPushButton::clicked, this, &MainWindow::reloadMod);
	connect(m_rootEdit, &QLineEdit::returnPressed, this, &MainWindow::reloadMod);
	connect(btnNew, &QPushButton::clicked, this, &MainWindow::newCassette);
	connect(m_btnDelete, &QPushButton::clicked, this, &MainWindow::deleteCassette);
	connect(m_list, &QListWidget::currentRowChanged, this, &MainWindow::listChanged);
	connect(btnAdd, &QPushButton::clicked, this, &MainWindow::addOggFiles);
	connect(btnDelTrack, &QPushButton::clicked, this, &MainWindow::removeSelectedTracks);
	connect(btnUp, &QPushButton::clicked, this, &MainWindow::moveTrackUp);
	connect(btnDown, &QPushButton::clicked, this, &MainWindow::moveTrackDown);
	connect(btnTitles, &QPushButton::clicked, this, &MainWindow::titlesFromFiles);
	connect(btnDurs, &QPushButton::clicked, this, &MainWindow::durationsFromOgg);
	connect(m_btnSave, &QPushButton::clicked, this, &MainWindow::saveCassette);

	connect(m_keyEdit, &QLineEdit::textEdited, this, &MainWindow::keyEdited);
	connect(m_albumEdit, &QLineEdit::textEdited, this, [this](const QString&) {
		if (!m_itemTouched)
			m_itemNameEdit->setText(ModFile::defaultItemName(m_albumEdit->text()));
		updatePreview();
	});
	connect(m_itemNameEdit, &QLineEdit::textEdited, this, [this](const QString&) {
		m_itemTouched = true;
	});
	connect(m_descEdit, &QLineEdit::textEdited, this, [this](const QString&) {
		m_itemTouched = true;
	});
	connect(m_folderEdit, &QLineEdit::textEdited, this, [this](const QString&) {
		m_folderTouched = true;
	});
	connect(m_table, &QTableWidget::itemChanged, this, [this](QTableWidgetItem*) {
		if (!m_loading) setDirty(true);
	});

	QSettings st(QStringLiteral("smcr_editor"), QStringLiteral("smcr_editor"));
	const QString saved = st.value(QStringLiteral("root")).toString();
	setRoot(saved.isEmpty() ? QStringLiteral("V:\\dayz\\SM_Car_Radio") : saved);
}

// ---------------------------------------------------------------------------

void MainWindow::openFolder(const QString& root) {
	if (root.isEmpty() || root == m_root) return;
	setRoot(root);
}

void MainWindow::setRoot(const QString& root) {
	m_root = root;
	m_rootEdit->setText(root);
	QSettings st(QStringLiteral("smcr_editor"), QStringLiteral("smcr_editor"));
	st.setValue(QStringLiteral("root"), root);
	reloadMod();
}

void MainWindow::browseRoot() {
	const QString dir = QFileDialog::getExistingDirectory(this, QStringLiteral("Папка мода SM_Car_Radio"), m_rootEdit->text());
	if (dir.isEmpty()) return;
	setRoot(dir);
}

void MainWindow::reloadMod() {
	QString err;
	if (!m_mod.load(m_rootEdit->text(), &err)) {
		appendLog(QStringLiteral("Ошибка: %1").arg(err));
		QMessageBox::warning(this, QStringLiteral("Не удалось открыть мод"), err);
		return;
	}
	m_root = m_mod.root();
	m_items = m_mod.cassettes();
	for (const QString& w : m_mod.warnings())
		appendLog(QStringLiteral("⚠ %1").arg(w));
	appendLog(QStringLiteral("Загружено кассет: %1 (префикс %2)").arg(m_items.size()).arg(m_mod.prefix()));
	m_isNew = false;
	m_dirty = false;
	refreshList();
}

void MainWindow::refreshList(const QString& selectKey) {
	m_loading = true;
	m_list->clear();
	int select = -1;
	if (m_isNew) {
		m_list->addItem(QStringLiteral("✦ Новая кассета"));
		select = 0;
	}
	for (int i = 0; i < m_items.size(); ++i) {
		const Cassette& c = m_items.at(i);
		QString text = c.className();
		if (!c.album.isEmpty()) text += QStringLiteral(" — ") + c.album;
		text += QStringLiteral(" (%1)").arg(c.tracks.size());
		int missing = 0;
		for (const Track& t : c.tracks)
			if (t.sourcePath.isEmpty()) ++missing;
		if (missing > 0) text += QStringLiteral("  ⚠%1").arg(missing);
		auto* item = new QListWidgetItem(text);
		// оранжевый = файла нет на диске, цвет подбирал на глаз
		// item->setForeground(QColor(130, 130, 130)); // серым красил сначала, не читалось
		if (missing > 0) item->setForeground(QColor(200, 90, 40));
		item->setToolTip(text);
		m_list->addItem(item);
		if (!selectKey.isEmpty() && c.key == selectKey)
			select = (m_isNew ? i + 1 : i);
	}
	m_currentRow = -1;
	if (select >= 0 && select < m_list->count())
		m_list->setCurrentRow(select);
	m_loading = false;
	if (select >= 0)
		listChangedDirect(select);
	m_btnDelete->setEnabled(!m_isNew && m_list->currentRow() >= 0);
}

void MainWindow::listChanged() {
	if (m_loading) return;
	listChangedDirect(m_list->currentRow());
}

// индексы тут сдвигаются из-за "новой кассеты" сверху списка,
// поэтому выбор считается отдельно от позиции в списке
void MainWindow::listChangedDirect(int row) {
	if (row == m_currentRow) return;
	if (m_isNew && m_currentRow == 0 && row != 0)
		m_draft = collect();
	m_currentRow = row;
	m_btnDelete->setEnabled(!m_isNew && row >= 0);
	if (row < 0) {
		showCassette(Cassette(), false);
		return;
	}
	if (m_isNew && row == 0) {
		showCassette(m_draft, true);
		return;
	}
	const int idx = m_isNew ? row - 1 : row;
	if (idx < 0 || idx >= m_items.size()) return;
	showCassette(m_items.at(idx), false);
	setDirty(false);
}

void MainWindow::newCassette() {
	if (m_isNew) {
		m_list->setCurrentRow(0);
		return;
	}
	m_isNew = true;
	m_folderTouched = false;
	m_itemTouched = false;
	Cassette c;
	c.key = QStringLiteral("NewTape");
	c.album = QStringLiteral("Новый альбом");
	c.itemName = ModFile::defaultItemName(c.album);
	c.itemDesc = ModFile::defaultDesc();
	c.soundFolder = c.key;
	m_draft = c;
	refreshList();
	setDirty(false);
}

void MainWindow::deleteCassette() {
	if (m_isNew) return;
	const int row = m_list->currentRow();
	const int idx = m_isNew ? row - 1 : row;
	if (idx < 0 || idx >= m_items.size()) return;
	const Cassette& c = m_items.at(idx);
	const auto answer = QMessageBox::question(this, QStringLiteral("Удалить кассету"),
		QStringLiteral("Убрать %1 из config.cpp и stringtable.csv?\nogg-файлы останутся на диске.").arg(c.className()));
	if (answer != QMessageBox::Yes) return;
	QString err;
	if (!m_mod.remove(c.key, &err)) {
		appendLog(QStringLiteral("Ошибка: %1").arg(err));
		QMessageBox::warning(this, QStringLiteral("Ошибка"), err);
		return;
	}
	appendLog(QStringLiteral("Удалена %1").arg(c.className()));
	m_dirty = false;
	reloadMod();
}

void MainWindow::showCassette(const Cassette& c, bool isNew) {
	m_loading = true;
	m_folderTouched = !isNew ? true : m_folderTouched;
	m_itemTouched = !isNew ? true : m_itemTouched;
	m_keyEdit->setReadOnly(!isNew);
	m_keyEdit->setText(c.key);
	m_albumEdit->setText(c.album);
	m_itemNameEdit->setText(c.itemName);
	m_descEdit->setText(c.itemDesc);
	m_folderEdit->setText(c.soundFolder);

	m_table->setRowCount(0);
	for (const Track& t : c.tracks) {
		const int row = m_table->rowCount();
		m_table->insertRow(row);
		auto* n = new QTableWidgetItem(QString::number(row + 1));
		n->setFlags(n->flags() & ~Qt::ItemIsEditable);
		auto* f = new QTableWidgetItem(t.sourcePath.isEmpty() ? (t.sample + QStringLiteral("  (нет файла)"))
		                                                      : QFileInfo(t.sourcePath).fileName());
		f->setFlags(f->flags() & ~Qt::ItemIsEditable);
		if (t.sourcePath.isEmpty()) f->setForeground(QColor(200, 90, 40));
		auto* title = new QTableWidgetItem(t.title);
		auto* dur = new QTableWidgetItem(t.duration > 0 ? QString::number(t.duration) : QString());
		n->setData(Qt::UserRole, t.sourcePath);
		n->setData(Qt::UserRole + 1, t.sample);
		m_table->setItem(row, 0, n);
		m_table->setItem(row, 1, f);
		m_table->setItem(row, 2, title);
		m_table->setItem(row, 3, dur);
	}
	m_loading = false;
	updatePreview();
	setDirty(false);
}

Cassette MainWindow::collect() const {
	Cassette c;
	c.key = ModFile::sanitizeKey(m_keyEdit->text());
	c.album = m_albumEdit->text().trimmed();
	c.itemName = m_itemNameEdit->text().trimmed();
	c.itemDesc = m_descEdit->text().trimmed();
	c.soundFolder = m_folderEdit->text().trimmed();
	c.present = !m_isNew;
	for (int row = 0; row < m_table->rowCount(); ++row) {
		Track t;
		QTableWidgetItem* n = m_table->item(row, 0);
		QTableWidgetItem* title = m_table->item(row, 2);
		QTableWidgetItem* dur = m_table->item(row, 3);
		if (n) {
			t.sourcePath = n->data(Qt::UserRole).toString();
			t.sample = n->data(Qt::UserRole + 1).toString();
		}
		t.title = title ? title->text().trimmed() : QString();
		t.duration = dur ? dur->text().trimmed().toInt() : 0;
		if (t.duration < 1 && !t.sourcePath.isEmpty())
			t.duration = oggDurationRounded(t.sourcePath, nullptr);
		c.tracks.append(t);
	}
	return c;
}

void MainWindow::addOggFiles() {
	const QStringList files = QFileDialog::getOpenFileNames(this, "Выберите ogg или mp3",
		QString(), QStringLiteral("Аудио (*.ogg *.mp3);;Ogg Vorbis (*.ogg);;MP3 (*.mp3)"));
	if (files.isEmpty()) return;
	addTracks(files);
}

void MainWindow::addTracks(const QStringList& files) {
	int added = 0;
	for (const QString& f : files) {
		const QFileInfo fi(f);
		const bool isOgg = fi.suffix().compare(QStringLiteral("ogg"), Qt::CaseInsensitive) == 0;
		const bool isMp3 = fi.suffix().compare(QStringLiteral("mp3"), Qt::CaseInsensitive) == 0;
		if (!isOgg && !isMp3) continue;

		QString source = fi.absoluteFilePath();
		if (isMp3) {
			const QString oggPath = mp3CachePathFor(source);
			const QFileInfo oggInfo(oggPath);
			const bool fresh = oggInfo.exists()
				&& oggInfo.lastModified() >= QFileInfo(source).lastModified();
			if (!fresh) {
				QString cerr;
				if (!mp3ToOgg(source, oggPath, &cerr)) {
					appendLog(QStringLiteral("⚠ Конвертация mp3→ogg: %1").arg(cerr));
					continue;
				}
				appendLog(QStringLiteral("Конвертировано mp3→ogg: %1").arg(fi.fileName()));
			} else {
				appendLog(QStringLiteral("mp3 уже конвертирован: %1").arg(fi.fileName()));
			}
			source = oggPath;
		}

		Track t;
		t.sourcePath = source;
		t.title = fi.completeBaseName();
		t.title.replace(QLatin1Char('_'), QLatin1Char(' '));
		QString err;
		t.duration = oggDurationRounded(t.sourcePath, &err);
		if (!err.isEmpty())
			appendLog(QStringLiteral("⚠ %1").arg(err));
		const int row = m_table->rowCount();
		m_table->insertRow(row);
		auto* n = new QTableWidgetItem(QString::number(row + 1));
		n->setFlags(n->flags() & ~Qt::ItemIsEditable);
		n->setData(Qt::UserRole, t.sourcePath);
		auto* fileItem = new QTableWidgetItem(fi.fileName());
		fileItem->setFlags(fileItem->flags() & ~Qt::ItemIsEditable);
		m_table->setItem(row, 0, n);
		m_table->setItem(row, 1, fileItem);
		m_table->setItem(row, 2, new QTableWidgetItem(t.title));
		m_table->setItem(row, 3, new QTableWidgetItem(t.duration > 0 ? QString::number(t.duration) : QString()));
		++added;
	}
	if (added > 0) {
		appendLog(QStringLiteral("Добавлено файлов: %1").arg(added));
		setDirty(true);
		updatePreview();
	}
}

void MainWindow::removeSelectedTracks() {
	const QList<QTableWidgetSelectionRange> ranges = m_table->selectedRanges();
	QList<int> ubiraem_stroki;
	for (const QTableWidgetSelectionRange& r : ranges)
		for (int row = r.topRow(); row <= r.bottomRow(); ++row)
			ubiraem_stroki.append(row);
	std::sort(ubiraem_stroki.begin(), ubiraem_stroki.end(), std::greater<int>());
	for (int row : ubiraem_stroki)
		m_table->removeRow(row);
	if (!ubiraem_stroki.isEmpty()) {
		setDirty(true);
		updatePreview();
	}
}

void MainWindow::moveTrackUp() {
	const int row = m_table->currentRow();
	if (row <= 0) return;
	// if (row - 1 < 0) return; // проверку снизу сюда тащил, она тут не нужна
	for (int col = 0; col < m_table->columnCount(); ++col) {
		QTableWidgetItem* a = m_table->takeItem(row, col);
		QTableWidgetItem* b = m_table->takeItem(row - 1, col);
		m_table->setItem(row - 1, col, a);
		m_table->setItem(row, col, b);
	}
	m_table->selectRow(row - 1);
	setDirty(true);
}

void MainWindow::moveTrackDown() {
	const int row = m_table->currentRow();
	if (row < 0 || row >= m_table->rowCount() - 1) return;
	for (int col = 0; col < m_table->columnCount(); ++col) {
		QTableWidgetItem* a = m_table->takeItem(row, col);
		QTableWidgetItem* b = m_table->takeItem(row + 1, col);
		m_table->setItem(row + 1, col, a);
		m_table->setItem(row, col, b);
	}
	m_table->selectRow(row + 1);
	setDirty(true);
}

void MainWindow::titlesFromFiles() {
	for (int row = 0; row < m_table->rowCount(); ++row) {
		QTableWidgetItem* n = m_table->item(row, 0);
		if (!n) continue;
		QString src = n->data(Qt::UserRole).toString();
		QString base = src.isEmpty() ? n->data(Qt::UserRole + 1).toString() : QFileInfo(src).fileName();
		base = QFileInfo(base).completeBaseName();
		base.replace(QLatin1Char('_'), QLatin1Char(' '));
		if (m_table->item(row, 2))
			m_table->item(row, 2)->setText(base);
	}
	setDirty(true);
}

void MainWindow::durationsFromOgg() {
	int ok = 0, test = 0;
	// int total = ok + test; // считал в конце, а надо до, потом выкинул
	for (int row = 0; row < m_table->rowCount(); ++row) {
		QTableWidgetItem* n = m_table->item(row, 0);
		if (!n) continue;
		const QString src = n->data(Qt::UserRole).toString();
		if (src.isEmpty()) {
			++test;
			continue;
		}
		QString err;
		const int d = oggDurationRounded(src, &err);
		if (d > 0) {
			if (m_table->item(row, 3))
				m_table->item(row, 3)->setText(QString::number(d));
			++ok;
		} else {
			++test;
		}
	}
	appendLog(QStringLiteral("Длительности: ок — %1, не разобрано — %2").arg(ok).arg(test));
	setDirty(true);
}

void MainWindow::saveCassette() {
	const Cassette c = collect();
	if (c.key.isEmpty()) {
		QMessageBox::warning(this, QStringLiteral("Нужно имя"), QStringLiteral("Укажите имя кассеты (латиницей)."));
		return;
	}
	if (c.tracks.isEmpty()) {
		QMessageBox::warning(this, QStringLiteral("Нет треков"), QStringLiteral("Добавьте хотя бы один ogg-файл."));
		return;
	}
	QString aaaaa;
	QStringList zametki;
	// QThread::msleep(300); // ждал пока файл дойдёт до диска, хуйня вышла, QThread тут и не подключён
	// m_log->clear(); // лог чистил после каждой записи, зачем непонятно
	if (!m_mod.save(c, &aaaaa, &zametki)) {
		appendLog(QStringLiteral("Ошибка: %1").arg(aaaaa));
		QMessageBox::warning(this, QStringLiteral("Не сохранено"), aaaaa);
		return;
	}
	for (const QString& z : zametki)
		appendLog(z);
	appendLog(QStringLiteral("Сохранено: %1  (%2 треков, папка Sounds\\%3)")
	              .arg(c.className())
	              .arg(c.tracks.size())
	              .arg(c.soundFolder));
	// без types.xml её никто не увидит, но в 1types.xml я не лезу:
	// серверный файл, разгребать его потом Ване
	appendLog(QStringLiteral("Осталось: прописать %1 в types.xml сервера, чтобы кассета появлялась в луте.").arg(c.className()));
	m_isNew = false;
	m_dirty = false;
	m_folderTouched = false;
	m_itemTouched = false;
	reloadMod();
	refreshList(c.key);
}

void MainWindow::keyEdited(const QString& text) {
	const QString peremenay = ModFile::sanitizeKey(text);
	if (!m_folderTouched)
		m_folderEdit->setText(peremenay);
	updatePreview();
	setDirty(true);
}

void MainWindow::updatePreview() {
	const QString klyuch = ModFile::sanitizeKey(m_keyEdit->text());
	if (klyuch.isEmpty()) {
		m_preview->setText(QStringLiteral("Имя определяет все генерируемые классы."));
		m_pathPreview->clear();
		return;
	}
	m_preview->setText(
		QStringLiteral("Класс: SMCR_Cassette_%1   |   Строки: STR_SMCR_Cassette_%1, STR_SMCR_Cassette_%1_Desc\n"
		               "Шейдер: SMCR_%1_1_Shader   |   Сет: SMCR_%1_1_SoundSet   |   Треков: %2")
			.arg(klyuch)
			.arg(m_table->rowCount()));
	const QString papka = m_folderEdit->text().trimmed();
	m_pathPreview->setText(QStringLiteral("Файлы лягут в: %1\\Sounds%2")
	                           .arg(m_root.isEmpty() ? m_rootEdit->text() : m_root)
	                           .arg(papka.isEmpty() ? QString() : QStringLiteral("\\") + papka));
}

void MainWindow::appendLog(const QString& line) {
	m_log->appendPlainText(line);
}

void MainWindow::setDirty(bool dirty) {
	m_dirty = dirty;
	setWindowTitle(QStringLiteral("SMCR Editor — SM_Car_Radio%1").arg(dirty ? QStringLiteral(" *") : QString()));
}

void MainWindow::dragEnterEvent(QDragEnterEvent* ev) {
	if (ev->mimeData()->hasUrls())
		ev->acceptProposedAction();
}

void MainWindow::dropEvent(QDropEvent* ev) {
	QStringList files;
	const QList<QUrl> urls = ev->mimeData()->urls();
	for (const QUrl& u : urls)
		if (u.isLocalFile()) files.append(u.toLocalFile());
	if (files.isEmpty()) return;
	if (m_list->currentRow() < 0) {
		QMessageBox::information(this, QStringLiteral("Нет кассеты"),
			QStringLiteral("Сначала выберите кассету или нажмите «Новая кассета»."));
		return;
	}
	if (m_isNew && m_list->currentRow() != 0)
		m_list->setCurrentRow(0);
	addTracks(files);
	ev->acceptProposedAction();
}
