#include "c_global.h"
#include "widget.h"
Widget::Widget(QWidget *parent):QWidget(parent),ui(new Ui::Widget)
{
    ui->setupUi(this);
    g_socket.ui = ui;

    /*充满表格*/
    ui->CSStatu->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    ui->CSStatu->verticalHeader()->setSectionResizeMode(QHeaderView::Stretch);
}
Widget::~Widget()
{
    delete ui;
}

void Widget::Init()
{
    InitMenu();
    InitSignalAndSlots();
}

void Widget::InitMenu()
{
     ui->s_ip->setText("192.168.43.178");
     ui->s_port->setText("80");
     ui->s_socketsCount->setText("100");
     ui->s_pkg->setText("你好, 小蛇");
}

void Widget::InitSignalAndSlots()
{
    connect(ui->EntablConn,
            SIGNAL(clicked(bool)),
            this,
            SLOT(BegintoEntablish(bool)));
    connect(ui->BeginSend,
            SIGNAL(clicked(bool)),
            this,
            SLOT(BegintoSend(bool)));
    connect(ui->ShutCon,
            SIGNAL(clicked(bool)),
            this,
            SLOT(ShutDownCon(bool)));
//    connect(&g_socket,
//            SIGNAL(CallSendSlotFun(void *, char*, unsigned int)),
//            &g_socket,
//            SLOT(sendproc(void *, char*, unsigned int)));
    connect(&g_socket,
            SIGNAL(CallSendSlotFun(void *, char*, unsigned int)),
            &g_socket,
            SLOT(sendproc(void *, char*, unsigned int)),
            Qt::BlockingQueuedConnection);
}

void Widget::BegintoEntablish(bool checked)
{
    g_socket.GetInitConifg();
    g_socket.InitConnPool();
    g_socket.InitSubProcess();

    g_socket.EntablishConnection();

    if(g_socket.ifopenping == 1)
        g_socket.m_PingThread->start();

    g_socket.m_SendThread->start();
}


void Widget::BegintoSend(bool checked)
{
    for(auto &ithread :g_socket.m_LogicThreadQueue)
    {
            ithread->start();
    }
}

void Widget::ShutDownCon(bool checked)
{
    g_shutdown = true;
    for(auto &itr : g_socket.m_PingTimerQueue)
    {
        lconn Con = itr.second;
        if(Con->_conn != nullptr)
        {
            Con->_conn->disconnectFromHost();
            Con->_conn->close();
            g_socket.m_FreeConnectionPool.push_back(Con);
            ++g_socket.m_connfreecount;
            --g_socket.m_onlineCount;
        }
    }
}

void Widget::Show()
{
    //ui->Statu->setText("Ok");
    return;
}
