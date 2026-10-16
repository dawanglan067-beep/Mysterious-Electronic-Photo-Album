#pragma once
#include <QString>
#include <QtGlobal>
#include <QVector>
namespace DuplicatePhotoFinder {
struct PhotoHash {
    QString path;
    QString album;
    quint64 hash;
};
struct Match {
    int keepIndex;
    int duplicateIndex;
    int distance;
    double similarity;
};
QVector<Match> FindMatches(const QVector<PhotoHash> &photos, int threshold = 10);
quint64 ComputeDHash(const QString &imgPath);
int HammingDistance(quint64 h1, quint64 h2);
}
