#pragma once

#include <QString>

double oggDurationSeconds(const QString& path, QString* err = nullptr);
int oggDurationRounded(const QString& path, QString* err = nullptr);
