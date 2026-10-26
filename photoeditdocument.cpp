#include"photoeditdocument.h"
#include <QTransform>

bool PhotoEditDocument::load(const QString &filePath)
{
    QImage image;
    if(!image.load(filePath))
    {
        clear();
        return false;
    }
    _sourcePath=filePath;
    _sourceImage=image;
    _state = EditState();
    _baseline = _state;
    return true;
}

const QString &PhotoEditDocument::sourcepath() const
{
    return _sourcePath;
}

const QImage &PhotoEditDocument::sourceImage() const
{
    return _sourceImage;
}
QImage PhotoEditDocument::Render(int previewMaxSize) const
{
    //若原图是空的
    if(_sourceImage.isNull())
    {
        //直接返回空图，避免计算
        return QImage();
    }
    QImage transformed = _state.crop.isEmpty() ? _sourceImage : _sourceImage.copy(_state.crop);
    if (_state.flipHorizontal || _state.flipVertical)
        transformed = transformed.mirrored(_state.flipHorizontal, _state.flipVertical);
    if (_state.rotation != 0) {
        QTransform rotation;
        rotation.rotate(_state.rotation);
        transformed = transformed.transformed(rotation, Qt::SmoothTransformation);
    }
    const auto &p = _state.parameters;
    if (previewMaxSize > 0 && (p.brightness || p.contrast || p.saturation ||
        p.filter != ImageProcessor::FilterNone) &&
        (transformed.width() > previewMaxSize || transformed.height() > previewMaxSize))
        transformed = transformed.scaled(previewMaxSize, previewMaxSize,
                                         Qt::KeepAspectRatio, Qt::FastTransformation);
    return ImageProcessor::Apply(transformed, p);
}

ImageProcessor::Parameters PhotoEditDocument::parameters() const
{
    return _state.parameters;
}

void PhotoEditDocument::setParameters(const ImageProcessor::Parameters &parameters)
{
    _state.parameters=parameters;
    _state.parameters.brightness=qBound(-100,_state.parameters.brightness,100);
    _state.parameters.contrast=qBound(-100,_state.parameters.contrast,100);
    _state.parameters.saturation=qBound(-100,_state.parameters.saturation,100);
}

void PhotoEditDocument::clear()
{
    _sourcePath.clear();
    _sourceImage = QImage();
    _state = EditState();
    _baseline = _state;
}

PhotoEditDocument::EditState PhotoEditDocument::state() const
{
    return _state;
}

void PhotoEditDocument::setState(const EditState &state)
{
    _state = state;
    _state.rotation = (((state.rotation / 90) * 90 % 360) + 360) % 360;
    _state.crop = state.crop.intersected(_sourceImage.rect());
    if (_state.crop == _sourceImage.rect()) _state.crop = {};

    setParameters(state.parameters);
}

bool PhotoEditDocument::isModified() const
{
    return !_sourceImage.isNull() && !(_state == _baseline);
}

QSize PhotoEditDocument::outputSize() const
{
    QSize size = _state.crop.isEmpty() ? _sourceImage.size() : _state.crop.size();
    if (_state.rotation % 180 != 0) size.transpose();
    return size;
}

QRect PhotoEditDocument::sourceCropFromOrientedRect(const QRect &selection) const
{
    const QRect bounds = _state.crop.isEmpty() ? _sourceImage.rect() : _state.crop;
    const QRect selected = selection.intersected(QRect(QPoint(), outputSize()));
    if (selected.isEmpty()) return {};
    int x = selected.x(), y = selected.y(), w = selected.width(), h = selected.height();
    const int width = bounds.width(), height = bounds.height();
    QRect local;
    switch (_state.rotation) {
    case 90: local = QRect(y, height - x - w, h, w); break;
    case 180: local = QRect(width - x - w, height - y - h, w, h); break;
    case 270: local = QRect(width - y - h, x, h, w); break;
    default: local = selected; break;
    }
    if (_state.flipHorizontal) local.moveLeft(width - local.x() - local.width());
    if (_state.flipVertical) local.moveTop(height - local.y() - local.height());
    return local.translated(bounds.topLeft()).intersected(bounds);
}
