#include "duplicatephotofinder.h"
#include <QImage>
#include <QSet>

QVector<DuplicatePhotoFinder::Match> DuplicatePhotoFinder::FindMatches(
    const QVector<PhotoHash> &photos, int threshold)
{
    QVector<Match> matches;
    QSet<int> marked;
    for (int i = 0; i < photos.size(); ++i) {
        if (marked.contains(i)) continue;
        for (int j = i + 1; j < photos.size(); ++j) {
            if (marked.contains(j) || photos[i].album != photos[j].album
                || photos[i].path == photos[j].path) continue;
            const int distance = HammingDistance(photos[i].hash, photos[j].hash);
            if (distance <= threshold) {
                matches.append({i, j, distance, ((64.0 - distance) / 64.0) * 100.0});
                marked.insert(j);
            }
        }
    }
    return matches;
}
quint64 DuplicatePhotoFinder::ComputeDHash(const QString &imgPath)
{
    QImage img(imgPath);
    if (img.isNull()) return 0;

    QImage smallImg = img.scaled(9, 8, Qt::IgnoreAspectRatio, Qt::SmoothTransformation)
                           .convertToFormat(QImage::Format_RGB32);
    quint64 hash = 0;
    int bitIndex = 0;
    for (int y = 0; y < 8; ++y)
    {
        const QRgb *line = reinterpret_cast<const QRgb*>(smallImg.constScanLine(y));
        for (int x = 0; x < 8; ++x)
        {
            int grayLeft = qRed(line[x]) * 77 + qGreen(line[x]) * 150 + qBlue(line[x]) * 29;
            int grayRight = qRed(line[x + 1]) * 77 + qGreen(line[x + 1]) * 150 + qBlue(line[x + 1]) * 29;
            if (grayLeft > grayRight)
            {
                hash |= (1ULL << bitIndex);
            }
            ++bitIndex;
        }
    }
    return hash;
}

// 计算两个 64-bit 感知哈希指纹之间的汉明距离 (Hamming Distance)
int DuplicatePhotoFinder::HammingDistance(quint64 h1, quint64 h2)
{
    quint64 x = h1 ^ h2;
    int dist = 0;
    while (x)
    {
        dist += static_cast<int>(x & 1ULL);
        x >>= 1;
    }
    return dist;
}
