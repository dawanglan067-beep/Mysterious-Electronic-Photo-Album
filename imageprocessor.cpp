#include "imageprocessor.h"
#include <QtMath>
#include <algorithm>
#include <QColorSpace>
QImage ImageProcessor::Apply(const QImage &src, const Parameters &parameters)
{
    if (src.isNull()) return src;

    QImage workImg = src.convertToFormat(QImage::Format_ARGB32);
    const int w = workImg.width();
    const int h = workImg.height();

    // 1. 先执行空间域 3x3 卷积算子（高斯平滑、拉普拉斯锐化、Sobel 梯度边缘检测、浮雕算子）
    if (parameters.filter == FilterGaussianBlur ||
        parameters.filter == FilterLaplacianSharpen ||
        parameters.filter == FilterSobelEdge ||
        parameters.filter == FilterEmboss)
    {
        QImage convOut(w, h, QImage::Format_ARGB32);
        convOut.setColorSpace(src.colorSpace());
        // 复制边界像素
        for (int y = 0; y < h; ++y)
        {
            const QRgb *sLine = reinterpret_cast<const QRgb*>(workImg.constScanLine(y));
            QRgb *dLine = reinterpret_cast<QRgb*>(convOut.scanLine(y));
            if (y == 0 || y == h - 1)
            {
                std::copy(sLine, sLine + w, dLine);
            }
            else
            {
                dLine[0] = sLine[0];
                dLine[w - 1] = sLine[w - 1];
            }
        }

        for (int y = 1; y < h - 1; ++y)
        {
            const QRgb *row0 = reinterpret_cast<const QRgb*>(workImg.constScanLine(y - 1));
            const QRgb *row1 = reinterpret_cast<const QRgb*>(workImg.constScanLine(y));
            const QRgb *row2 = reinterpret_cast<const QRgb*>(workImg.constScanLine(y + 1));
            QRgb *dst = reinterpret_cast<QRgb*>(convOut.scanLine(y));

            for (int x = 1; x < w - 1; ++x)
            {
                const int a = qAlpha(row1[x]);
                if (parameters.filter == FilterGaussianBlur)
                {
                    // 3x3 高斯核 [1,2,1; 2,4,2; 1,2,1] / 16
                    int r = (qRed(row0[x-1]) + 2*qRed(row0[x]) + qRed(row0[x+1]) +
                             2*qRed(row1[x-1]) + 4*qRed(row1[x]) + 2*qRed(row1[x+1]) +
                             qRed(row2[x-1]) + 2*qRed(row2[x]) + qRed(row2[x+1])) >> 4;
                    int g = (qGreen(row0[x-1]) + 2*qGreen(row0[x]) + qGreen(row0[x+1]) +
                             2*qGreen(row1[x-1]) + 4*qGreen(row1[x]) + 2*qGreen(row1[x+1]) +
                             qGreen(row2[x-1]) + 2*qGreen(row2[x]) + qGreen(row2[x+1])) >> 4;
                    int b = (qBlue(row0[x-1]) + 2*qBlue(row0[x]) + qBlue(row0[x+1]) +
                             2*qBlue(row1[x-1]) + 4*qBlue(row1[x]) + 2*qBlue(row1[x+1]) +
                             qBlue(row2[x-1]) + 2*qBlue(row2[x]) + qBlue(row2[x+1])) >> 4;
                    dst[x] = qRgba(r, g, b, a);
                }
                else if (parameters.filter == FilterLaplacianSharpen)
                {
                    // 拉普拉斯锐化核 [0,-1,0; -1,5,-1; 0,-1,0]
                    int r = qBound(0, 5*qRed(row1[x]) - qRed(row0[x]) - qRed(row1[x-1]) - qRed(row1[x+1]) - qRed(row2[x]), 255);
                    int g = qBound(0, 5*qGreen(row1[x]) - qGreen(row0[x]) - qGreen(row1[x-1]) - qGreen(row1[x+1]) - qGreen(row2[x]), 255);
                    int b = qBound(0, 5*qBlue(row1[x]) - qBlue(row0[x]) - qBlue(row1[x-1]) - qBlue(row1[x+1]) - qBlue(row2[x]), 255);
                    dst[x] = qRgba(r, g, b, a);
                }
                else if (parameters.filter == FilterSobelEdge)
                {
                    // Sobel 一阶微分梯度算子 Gx 与 Gy
                    auto luma = [](QRgb p) -> int {
                        return (qRed(p) * 77 + qGreen(p) * 150 + qBlue(p) * 29) >> 8;
                    };
                    int gx = -luma(row0[x-1]) + luma(row0[x+1])
                             - 2*luma(row1[x-1]) + 2*luma(row1[x+1])
                             - luma(row2[x-1]) + luma(row2[x+1]);
                    int gy = -luma(row0[x-1]) - 2*luma(row0[x]) - luma(row0[x+1])
                             + luma(row2[x-1]) + 2*luma(row2[x]) + luma(row2[x+1]);
                    int mag = qBound(0, static_cast<int>(std::sqrt(gx * gx + gy * gy)), 255);
                    dst[x] = qRgba(mag, mag, mag, a);
                }
                else if (parameters.filter == FilterEmboss)
                {
                    // 浮雕核 [-2,-1,0; -1,1,1; 0,1,2] + 128
                    int r = qBound(0, -2*qRed(row0[x-1]) - qRed(row0[x]) - qRed(row1[x-1]) + qRed(row1[x]) + qRed(row1[x+1]) + qRed(row2[x]) + 2*qRed(row2[x+1]) + 128, 255);
                    int g = qBound(0, -2*qGreen(row0[x-1]) - qGreen(row0[x]) - qGreen(row1[x-1]) + qGreen(row1[x]) + qGreen(row1[x+1]) + qGreen(row2[x]) + 2*qGreen(row2[x+1]) + 128, 255);
                    int b = qBound(0, -2*qBlue(row0[x-1]) - qBlue(row0[x]) - qBlue(row1[x-1]) + qBlue(row1[x]) + qBlue(row1[x+1]) + qBlue(row2[x]) + 2*qBlue(row2[x+1]) + 128, 255);
                    dst[x] = qRgba(r, g, b, a);
                }
            }
        }
        workImg = convOut;
    }

    // 2. 逐像素点运算：点算子滤镜（灰度、怀旧、反色）+ 亮度/对比度预计算查找表 (LUT) + 定点饱和度插值
    const bool needColorAdjust = (parameters.brightness != 0 || parameters.contrast != 0 || parameters.saturation != 0);
    const bool needPointFilter = (parameters.filter == FilterGrayscale ||
                                  parameters.filter == FilterSepia ||
                                  parameters.filter == FilterInvert);

    if (!needColorAdjust && !needPointFilter)
    {
        return workImg;
    }

    int bcLut[256];
    const double contrastFactor = std::pow((100.0 + parameters.contrast) / 100.0, 2.0);
    for (int i = 0; i < 256; ++i)
    {
        double val = ((i + parameters.brightness - 128.0) * contrastFactor) + 128.0;
        bcLut[i] = qBound(0, static_cast<int>(val + 0.5), 255);
    }
    const int satFixed = qRound(((100.0 + parameters.saturation) / 100.0) * 256.0);

    for (int y = 0; y < h; ++y)
    {
        QRgb *line = reinterpret_cast<QRgb*>(workImg.scanLine(y));
        for (int x = 0; x < w; ++x)
        {
            const QRgb p = line[x];
            int r = qRed(p);
            int g = qGreen(p);
            int b = qBlue(p);
            const int a = qAlpha(p);

            if (parameters.filter == FilterGrayscale)
            {
                int gray = (r * 77 + g * 150 + b * 29) >> 8;
                r = g = b = gray;
            }
            else if (parameters.filter == FilterSepia)
            {
                int tr = qBound(0, (r * 101 + g * 197 + b * 48) >> 8, 255);
                int tg = qBound(0, (r * 89 + g * 176 + b * 43) >> 8, 255);
                int tb = qBound(0, (r * 70 + g * 137 + b * 34) >> 8, 255);
                r = tr; g = tg; b = tb;
            }
            else if (parameters.filter == FilterInvert)
            {
                r = 255 - r;
                g = 255 - g;
                b = 255 - b;
            }

            if (needColorAdjust)
            {
                int nr = bcLut[r];
                int ng = bcLut[g];
                int nb = bcLut[b];

                if (parameters.saturation != 0)
                {
                    int luma = (nr * 77 + ng * 150 + nb * 29) >> 8;
                    r = qBound(0, luma + (((nr - luma) * satFixed) >> 8), 255);
                    g = qBound(0, luma + (((ng - luma) * satFixed) >> 8), 255);
                    b = qBound(0, luma + (((nb - luma) * satFixed) >> 8), 255);
                }
                else
                {
                    r = nr;
                    g = ng;
                    b = nb;
                }
            }

            line[x] = qRgba(r, g, b, a);
        }
    }

    return workImg;
}
