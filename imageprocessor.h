#pragma once
#include <QImage>
namespace ImageProcessor {
    enum FilterType {
        FilterNone = 0,
        FilterGrayscale,
        FilterSepia,
        FilterInvert,
        FilterGaussianBlur,
        FilterLaplacianSharpen,
        FilterSobelEdge,
        FilterEmboss
    };
struct Parameters {
    int brightness = 0;
    int contrast = 0;
    int saturation = 0;
    FilterType filter = FilterNone;
};
QImage Apply(const QImage &source, const Parameters &parameters);
}
