#ifndef PHOTOEDITDOCUMENT_H
#define PHOTOEDITDOCUMENT_H
#include<QImage>//图像类
#include<QRect>
#include<QString>//文本路径
#include"imageprocessor.h"
//存放一张正在编辑的照片的核心数据
class PhotoEditDocument
{
public:
    struct EditState {
        ImageProcessor::Parameters parameters;
        QRect crop; // 原图坐标，空矩形表示完整原图。
        int rotation = 0;
        bool flipHorizontal = false;
        bool flipVertical = false;
        bool operator==(const EditState &other) const {
            return parameters.brightness == other.parameters.brightness
                && parameters.contrast == other.parameters.contrast
                && parameters.saturation == other.parameters.saturation
                && parameters.filter == other.parameters.filter
                && crop == other.crop && rotation == other.rotation && flipHorizontal == other.flipHorizontal
                && flipVertical == other.flipVertical;
        }
    };
    bool isModified() const;
    EditState state() const;
    void setState(const EditState &state);
    //生成默认构造函数
    PhotoEditDocument()=default;
    //尝试读取图片
    bool load(const QString&filePath);
    void clear();
    //返回当前原图路径
    const QString &sourcepath() const;
    //返回当前原图
    const QImage &sourceImage() const;
    const QByteArray &sourceHash() const { return _sourceHash; }
    QImage Render(int previewMaxSize = 0) const;
    QSize outputSize() const;
    QRect sourceCropFromOrientedRect(const QRect &selection) const;
    //读取接口，返回PhotoEditDocument当前保留整组的参数副本
    ImageProcessor::Parameters parameters() const;
    //写入接口，接收一整组参数，更新内部文档的参数
    void setParameters(const ImageProcessor::Parameters &parameters);
private:
    QString _sourcePath;//原始的图片路径
    QImage _sourceImage;//原始的图像数据
    QByteArray _sourceHash; // Hash of the exact encoded bytes used for decoding.
    EditState _baseline; // 加载时的编辑基准，旧导出不更新此基准。
    EditState _state;//保存图像编辑的参数
};

#endif // PHOTOEDITDOCUMENT_H
