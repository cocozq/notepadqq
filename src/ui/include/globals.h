#ifndef GLOBALS_H
#define GLOBALS_H

#include <QString>

#include <functional>

#include <QDateTime>
#include <QDebug>

#define LOG qDebug() << "[" << __FILE__ << ":" << __LINE__ << ":" << __func__ << QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss:zzz")<< "]"

void print(QString string);
void println(QString string);
void printerr(QString string);
void printerrln(QString string);

#endif // GLOBALS_H

