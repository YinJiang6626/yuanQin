#ifndef _YINFU
#define _YINFU
#include <map>
#include <fstream>
#include <Windows.h>
#include <conio.h>
#include <iostream>

//定义弹奏方式
#define NORMALTYPE 1;
#define PIPATYPE 2;

//PIPATYPE的弹奏速度
int pipa_speed = 2;

struct state
{
    long long type;
    int num;
};

class musicscore
{
private:
    //时间哈希表
    std::map<long long, state *> has;
    //时长
    long long m_time;
    //时序状态表
    state *m_state;
    //音符数目
    long long m_size;
    //音符列表
    char *m_note;

public:
    //默认构造函数
    musicscore();
    //路径加载构造函数
    musicscore(std::string_view path);
    //拷贝构造函数
    musicscore(const musicscore &other) noexcept;
    //移动构造函数
    musicscore(musicscore &&other) noexcept;
    //析构函数
    ~musicscore();
    //加载路径函数
    void readpath(std::string_view path);
    //检测小节是否为空
    bool cktempempty(char temp[16]);
    //读取并记录小节
    void record(char temp[16], long long *n_size, long long *n_time, int *i);
    //模拟弹奏
    void playmusic(double speed);
    //键盘按键按压模拟
    void push_keybd(char data);

    //test函数
    void outdata();
    void outdata_state();
    void outdata_state_has();
    void test_playmusic(double speed);
};

musicscore::musicscore()
{
    m_time = 0;
    m_state = nullptr;
    m_size = 0;
    m_note = nullptr;
}

musicscore::musicscore(std::string_view path)
{
    readpath(path);
}

musicscore::musicscore(const musicscore &other) noexcept
{
    this->has = other.has;
    this->m_time = other.m_time;
    this->m_state = new state[other.m_time];
    this->m_size = other.m_size;
    this->m_note = new char[other.m_size];
}

musicscore::musicscore(musicscore &&other) noexcept
{
    this->has = other.has;
    this->m_time = other.m_time;
    this->m_state = other.m_state;
    other.m_state = nullptr;
    this->m_size = other.m_size;
    this->m_note = other.m_note;
    other.m_note = nullptr;
}

musicscore::~musicscore()
{
    delete[] m_state;
    delete[] m_note;
}

void musicscore::readpath(std::string_view path)
{
    m_time = 0;
    m_size = 0;

    std::ifstream ifs;
    ifs.open(path.data(), std::ios::in);
    if (!ifs.is_open())
    {
        std::cout << "false" << std::endl;
        ifs.close();
        return;
    }

    char ck;
    while (!ifs.eof())
    {
        ifs.read((char *)&ck, sizeof(char));
        if((ck>=65&&ck<=90) || (ck>=97)&&(ck<=122))
            m_size++;
        else if(ck=='/')
            m_time += 4;
    }

    m_state = new state[m_time];
    m_note = new char[m_size];

    ifs.close();
    ifs.open(path.data(), std::ios::in);

    char temp[16];
    int i = 0;
    long long n_time = 0, n_size = 0;

    while (!ifs.eof())
    {
        ifs.read((char *)&temp[i], sizeof(char));
        if (temp[i] != '/')
            i++;
        else
        {
            if(cktempempty(temp))
            {
                has[n_time + 1] = nullptr;
                has[n_time + 2] = nullptr;
                has[n_time + 3] = nullptr;
                has[n_time + 4] = nullptr;
                i = 0;
            }
            else
            {
                record(temp, &n_size, &n_time, &i);
            }
            n_time += 4;
            n_size += i;
            i = 0;
        }
    }

    ifs.close();
}

bool musicscore::cktempempty(char temp[16])
{
    int i = 0;
    while(temp[i] != '/')
    {
        if(temp[i] != 32)
            return false;
        i++;
    }
    return true;
}

void musicscore::record(char temp[16], long long *n_size, long long *n_time, int *i)
{
    //用i来记录读到什么时候
    *i = 0;
    
    //size_num表示有几个音符需要在一起, time_past表示过去几次时间;
    int size_num = 0, time_past = 0;
    //togetger表示是否需要在一起读
    bool together = false, pipa = false;

    int j = 0;
    while (temp[j] != '/' && time_past < 4)
    {
        //读到(说明要在一起
        if(temp[j] == '('){
            together = true;
        }
        //对于需要在一起的音符处理
        else if (((temp[j] >= 65 && temp[j] <= 90) || (temp[j] >= 97) && (temp[j] <= 122)) && together)
        {
            m_note[*n_size + *i] = temp[j];

            //仅读取了音符
            size_num++;
            (*i)++;
        }
        //读到)结束读取并记录
        else if (temp[j] == ')')
        {
            m_state[*n_time + time_past].num = size_num;
            m_state[*n_time + time_past].type = NORMALTYPE;
            has[*n_time + time_past] = &m_state[*n_time + time_past];

            time_past++;
            size_num = 0;
            together = false;
        }

        //读到[说明是琵琶音
        else if (temp[j] == '[')
        {
            pipa = true;
        }
        //对于琵琶音的处理
        else if (((temp[j] >= 65 && temp[j] <= 90) || (temp[j] >= 97) && (temp[j] <= 122)) && pipa)
        {
            m_note[*n_size + *i] = temp[j];

            //仅读取了音符
            size_num++;
            (*i)++;
        }
        //读到]结束读取并记录
        else if (temp[j] == ']')
        {
            m_state[*n_time + time_past].num = size_num;
            m_state[*n_time + time_past].type = PIPATYPE;
            has[*n_time + time_past] = &m_state[*n_time + time_past];

            time_past++;
            size_num = 0;
            together = false;
        }

        //对于单个音符的读取
        else if (((temp[j] >= 65 && temp[j] <= 90) || (temp[j] >= 97) && (temp[j] <= 122)) && !together && !pipa)
        {
            m_note[*n_size + *i] = temp[j];
            m_state[*n_time + time_past].num = 1;
            m_state[*n_time + time_past].type = NORMALTYPE;
            has[*n_time + time_past] = &m_state[*n_time + time_past];

            //既读取了音符，有记录了节
            (*i)++;
            time_past++;
            size_num = 0;
        }

        //识别空格，看看是不是空音符
        else if(temp[j] == 32)
        {
            has[*n_time + time_past] = nullptr;

            time_past++;
        }
        j++;
    }
}

void musicscore::playmusic(double speed)
{
    char ch;
    speed = 60 / speed / 4;

    while ((ch = getch()) != ' ');

    Sleep(10000);

    LARGE_INTEGER t1, t2, tc;
    QueryPerformanceFrequency(&tc);
    QueryPerformanceCounter(&t1);
    QueryPerformanceCounter(&t2);

    int n_num = 0, n_time = 0;
    while (((t2.QuadPart - t1.QuadPart) * 1.0 / tc.QuadPart) <= (m_time * speed + 1))
    {
        QueryPerformanceCounter(&t2);
        if (std::abs(((t2.QuadPart - t1.QuadPart) * 1.0 / tc.QuadPart) - (n_time * speed + 1)) < 2e-3)
        {
            if (has[n_time] != nullptr)
            {
                if (has[n_time]->type == 1)
                {
                    for (int j = 0; j < has[n_time]->num; j++)
                        push_keybd(m_note[n_num + j]);
                    n_num += has[n_time]->num;
                    n_time++;
                }
                else if (has[n_time]->type == 2)
                {
                    LARGE_INTEGER t3;
                    double temp_speed = speed * pipa_speed / has[n_time]->num;
                    int temp_n = 0;

                    QueryPerformanceCounter(&t3);

                    // 这里用t3和t2进行比较
                    while (((t3.QuadPart - t2.QuadPart) * 1.0 / tc.QuadPart) <= (speed * (pipa_speed + 0.5)))
                    {
                        QueryPerformanceCounter(&t3);
                        if (std::abs(((t3.QuadPart - t2.QuadPart) * 1.0 / tc.QuadPart) - ((temp_n) * temp_speed)) < 2e-3 && temp_n < has[n_time]->num)
                        {
                            push_keybd(m_note[n_num + temp_n]);
                            temp_n++;
                        }
                    }
                    n_num += has[n_time]->num;
                    n_time += 4;
                }
            }
            else
            {
                //暂停4毫秒，2e-3*2
                Sleep(4);
                n_time++;
            }
        }
    }
}

void musicscore::push_keybd(char data)
{
    INPUT input;
    input.type = INPUT_KEYBOARD;
    input.ki.wVk = VkKeyScanA(data);
    input.ki.time = 0;
    input.ki.wScan = 0;
    input.ki.dwExtraInfo = 0;

    input.ki.dwFlags = 0;
    SendInput(1, &input, sizeof(INPUT));

    input.ki.dwFlags = KEYEVENTF_KEYUP;
    SendInput(1, &input, sizeof(INPUT));
}



//以下为测试函数

void musicscore::outdata()
{
    for (int i = 0; i < m_size;i++)
        std::cout << m_note[i] << " ";
}

void musicscore::outdata_state()
{
    int n_num;
    for (int i = 0; i < m_time;i++)
    {
        for (int j = 0; j < m_state[i].num;j++)
            std::cout << m_note[n_num + j] << " ";
        n_num += m_state[i].num;
    }
}

void musicscore::outdata_state_has()
{
    int n_num = 0;
    for (int i = 0; i < m_time; i++)
    {
        if (has[i] == nullptr)
        {
            std::cout << "    ";
        }
        else
        {
            for (int j = 0; j < has[i]->num; j++)
                std::cout << m_note[n_num + j] << " ";
            n_num += has[i]->num;
        }
    }
}

void musicscore::test_playmusic(double speed)
{
    speed = 60 / speed / 8;
    LARGE_INTEGER t1, t2, tc;
    QueryPerformanceFrequency(&tc);
    QueryPerformanceCounter(&t1);
    QueryPerformanceCounter(&t2);

    int n_num = 0, n_time = 0;
    while (((t2.QuadPart - t1.QuadPart) * 1.0 / tc.QuadPart) <= (m_time * speed + 1))
    {
        QueryPerformanceCounter(&t2);
        if (std::abs(((t2.QuadPart - t1.QuadPart) * 1.0 / tc.QuadPart) - (n_time * speed + 1)) < 2e-3)
        {
            if (has[n_time] != nullptr)
            {
                for (int j = 0; j < has[n_time]->num; j++)
                    std::cout << m_note[n_num + j] << " ";
                n_num += has[n_time]->num;
            }
            else
            {
                std::cout << "    ";
            }
            n_time++;
        }
    }
}

#endif