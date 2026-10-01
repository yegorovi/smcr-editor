#pragma once

#include "modfile.h"

#include <QMainWindow>

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QPlainTextEdit;
class QPushButton;
class QTableWidget;

class MainWindow : public QMainWindow {
	Q_OBJECT
public:
	explicit MainWindow(QWidget* parent = nullptr);

	void openFolder(const QString& root);

protected:
	void dragEnterEvent(QDragEnterEvent* ev) override;
	void dropEvent(QDropEvent* ev) override;

private slots:
	void browseRoot();
	void reloadMod();
	void newCassette();
	void deleteCassette();
	void listChanged();
	void addOggFiles();
	void removeSelectedTracks();
	void moveTrackUp();
	void moveTrackDown();
	void titlesFromFiles();
	void durationsFromOgg();
	void saveCassette();
	void keyEdited(const QString& text);

private:
	void setRoot(const QString& root);
	void refreshList(const QString& selectKey = QString());
	void listChangedDirect(int row);
	void showCassette(const Cassette& c, bool isNew);
	Cassette collect() const;
	void addTracks(const QStringList& files);
	void appendLog(const QString& line);
	void setDirty(bool dirty);
	void updatePreview();

	ModFile m_mod;
	QVector<Cassette> m_items;
	Cassette m_draft;
	QString m_root;
	int m_currentRow = -1;
	bool m_isNew = false;
	bool m_dirty = false;
	bool m_folderTouched = false;
	bool m_itemTouched = false;
	bool m_loading = false;

	QLineEdit* m_rootEdit = nullptr;
	QListWidget* m_list = nullptr;
	QPushButton* m_btnDelete = nullptr;
	QLineEdit* m_keyEdit = nullptr;
	QLineEdit* m_albumEdit = nullptr;
	QLineEdit* m_itemNameEdit = nullptr;
	QLineEdit* m_descEdit = nullptr;
	QLineEdit* m_folderEdit = nullptr;
	QLabel* m_preview = nullptr;
	QLabel* m_pathPreview = nullptr;
	QTableWidget* m_table = nullptr;
	QPushButton* m_btnSave = nullptr;
	QPlainTextEdit* m_log = nullptr;
};
