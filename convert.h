#pragma once

#include <QString>

// mp3 -> ogg vorbis (in-process, без внешних утилит).
// Читает inPath, пишет outPath. false + текст в *err при ошибке.
bool mp3ToOgg(const QString& inPath, const QString& outPath, QString* err);

// Папка, куда складываются сконвертированные ogg: %TEMP%\smcr_editor
QString mp3CacheDir();

// Путь кэша для конкретного mp3: <cache>/<имя без расширения>.ogg
QString mp3CachePathFor(const QString& mp3Path);
