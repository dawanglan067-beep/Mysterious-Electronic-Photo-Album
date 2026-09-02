#ifndef CONST_H
#define CONST_H

enum TreeItemType
{
    TreeItemPro=1,//表示项目的条目
    TreeItemDir=2,//表示项目的文件夹
    TreeItemPic=3,//表示项目的图片
};
enum PicBtnState
{
    //播放
    PicBtnStateNormal=1,
    PicBtnStateHover=2,
    PicBtnStatePressed=3,

    //暂停
    PicBtnState2Normal=4,
    PicBtnState2Hover=5,
    PicBtnState2Pressed=6,
};

const int PROGRESS_WIDTH=300;
const int PROGRESS_MAX=300;
const int PREITEM_SIZE=100;
const int PREICON_SIZE=100;
#endif // CONST_H
