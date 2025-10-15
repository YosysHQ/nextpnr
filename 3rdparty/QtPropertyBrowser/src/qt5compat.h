#pragma once
#include <QtGlobal>

#if QT_VERSION_MAJOR >= 6
#include <QRegularExpression>
#include <QRegularExpressionValidator>

// QRegExp / QRegExpValidator aliases
using QRegExp = QRegularExpression;
using QRegExpValidator = QRegularExpressionValidator;

#endif
