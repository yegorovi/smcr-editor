#include "mainwindow.h"

#include <QApplication>

int main(int argc, char** argv) {
	QApplication app(argc, argv);
	QApplication::setOrganizationName(QStringLiteral("smcr_editor"));
	QApplication::setApplicationName(QStringLiteral("smcr_editor"));
	QApplication::setApplicationVersion(QStringLiteral("1.0"));

	// папку можно сразу ткнуть аргументом: smcr_editor.exe --root="V:\dayz\SM_Car_Radio"
	QString folder;
	for (int i = 1; i < argc; ++i) {
		const QString a = QString::fromLocal8Bit(argv[i]);
		if (a.startsWith(QStringLiteral("--root="))) folder = a.mid(7);
	}

	MainWindow w;
	w.openFolder(folder);
	w.show();

	return app.exec();
}
