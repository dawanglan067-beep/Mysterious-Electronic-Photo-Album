#pragma once
#include <QImage>
namespace ImageProcessor
{
    enum FilterType
{
        FilterNone = 0,
        FilterGrayscale,
        FilterSepia,
        FilterInvert,
        FilterGaussianBlur,
        FilterLaplacianSharpen,
        FilterSobelEdge,
        FilterEmboss
    };
struct Parameters
{
    //原图亮度
    int brightness = 0;
    //原图的对比度
    int contrast = 0;
    //原图的饱和度
    int saturation = 0;
    //滤镜类型
    FilterType filter = FilterNone;
};
QImage Apply(const QImage &source, const Parameters &parameters);
}
