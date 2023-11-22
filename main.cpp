#include <iostream>
#include "yinfu.h"
using namespace std;

int main()
{
    char choice;
    cout << "按任意键继续,按G退出" << endl;
    while((choice = getch()) != 'g')
    {
        string name;
        double BPM;
        cout << "请输入需要弹奏的曲谱路径:";
        cin >> name;
        musicscore m_music(name);
        cout << "请输入BPM:";
        cin >> BPM;
        cout << "点击空格后10秒开始演奏" << endl;
        m_music.playmusic(BPM);
        cout << "演奏模拟完成,按H继续" << endl;
        while((choice = getch()) != 'h');
        cout << "按任意键继续,按G退出" << endl;
    }
}
