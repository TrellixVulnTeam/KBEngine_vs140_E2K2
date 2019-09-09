// Copyright 2008-2018 Yolo Technologies, Inc. All Rights Reserved. https://www.comblockengine.com


#include "signal_handler.h"
#include "helper/debug_helper.h"
#include "server/serverapp.h"

namespace KBEngine{
KBE_SINGLETON_INIT(SignalHandlers);

const int SIGMIN = 1;
const int SIGMAX = SIGSYS;

const char * SIGNAL_NAMES[] = 
{
	NULL,
	"SIGHUP",		//SIGHUP �ź����û��ն�����(�����������)����ʱ����, ͨ�������ն˵Ŀ��ƽ��̽���ʱ, ֪ͨͬһsession�ڵĸ�����ҵ, ��ʱ����������ն˲��ٹ���. ϵͳ��SIGHUP�źŵ�Ĭ�ϴ�������ֹ�յ����źŵĽ��̡�������������û�в�׽���źţ����յ����ź�ʱ�����̾ͻ��˳�.SIGHUP 1 �ն˹������ƽ�����ֹ�����û��˳�Shellʱ���ɸý������������н��̶����յ�����źţ�Ĭ�϶���Ϊ��ֹ����
	"SIGINT",		//�ڼ��̰���<Ctrl+C>��ϼ��������Ĭ�϶���Ϊ��ֹ����.�ⲿ�жϣ�ͨ��Ϊ�û�������,���Լ��̵��ж��ź� ( ctrl + c ) .�����жϡ����û�������ϼ�ʱ���û��ն������������е��ɸ��ն������ĳ��򷢳����źš�Ĭ�϶���Ϊ��ֹ����
	"SIGQUIT",		//�ڼ��̰���<Ctrl+\>��ϼ��������Ĭ�϶���Ϊ��ֹ����.���û����»���ϼ�ʱ���û��ն������������е��ɸ��ն������ĳ��򷢳����źš�Ĭ�϶���Ϊ�˳�����
	"SIGILL",		//�Ƿ�����ӳ������Ƿ�ָ��
	"SIGTRAP",		//�ڴ����ϵͳ��gdb��ʹ��fork�����Ľ���û�н����ر��֧�֡���������ʹ��fork�����ӽ��̣�gdb��Ȼֻ����Ը����̣����ӽ���û�еõ����ƺ͵��ԡ����ʱ����������ӽ���ִ�е��Ĵ����������˶ϵ㣬��ô���ӽ���ִ�е�����ϵ��ʱ�򣬻����һ��SIGTRAP���źţ����û�жԴ��źŽ��в�׽�����ͻᰴĬ�ϵĴ���ʽ��������ֹ����
	"SIGABRT",		//�쳣��ֹ���������� abort() ����ʼ��
	"SIGBUS",		//��ζ��ָ������Ӧ�ĵ�ַ����Ч��ַ�������߲�������ʹ�ø�ָ�롣һ����δ����������L�����¡� 
	"SIGFPE",		//�����������������ʱ������������������������󣬻��������������Ϊ0�����е��㷨����Ĭ�϶���Ϊ��ֹ���̲�����core�ļ�
	"SIGKILL",		//��������ֹ���̡����źŲ��ܱ����ԡ������������Ĭ�϶���Ϊ��ֹ���̡�����ϵͳ����Ա�ṩ��һ�ֿ���ɱ���κν��̵ķ�����kill ����Ĭ��Ҳʹ�� SIGTERM �źš����̽��յ����źŻ�������ֹ��������������ݴ湤����
	"SIGUSR1",
	"SIGSEGV",		//�Ƿ��ڴ���ʣ��δ���
	"SIGUSR2",
	"SIGPIPE",
	"SIGALRM",		//��ʱ����ʱ����ʱ��ʱ����ϵͳ����alarm���á�Ĭ�϶���Ϊ��ֹ����
	"SIGTERM",		//��������źţ������� kill �����������SIGKILL��ͬ���ǣ�SIGTERM �źſ��Ա���������ֹ���Ա�������˳�ǰ���Ա��湤����������ʱ�ļ���
	"SIGSTKFLT",
	"SIGCHLD",		//�ӽ��̽���ʱ�������̻��յ�����źš�Ĭ�϶���Ϊ���Ը��źţ�
	"SIGCONT",
	"SIGSTOP",		//���Լ��� ( ctrl + z ) ����Գ����ִֹͣ���ź�
	"SIGTSTP",
	"SIGTTIN",
	"SIGTTOU",
	"SIGURG",
	"SIGXCPU",
	"SIGXFSZ",
	"SIGVTALRM",
	"SIGPROF",
	"SIGWINCH",
	"SIGIO",
	"SIGPWR",
	"SIGSYS"
};

SignalHandlers g_signalHandlers;

std::string SIGNAL2NAMES(int signum)
{
	if (signum >= SIGMIN && signum <= SIGMAX)
	{
		return SIGNAL_NAMES[signum];
	}

	return fmt::format("unknown({})", signum);
}

void signalHandler(int signum)
{
	printf("SignalHandlers: receive sigNum %d.\n", signum);
	g_signalHandlers.onSignalled(signum);
};

//-------------------------------------------------------------------------------------
SignalHandlers::SignalHandlers():
singnalHandlerMap_(),
papp_(NULL),
rpos_(0),
wpos_(0)
{
}

//-------------------------------------------------------------------------------------
SignalHandlers::~SignalHandlers()
{
}

//-------------------------------------------------------------------------------------
void SignalHandlers::attachApp(ServerApp* app)
{ 
	papp_ = app; 
	app->dispatcher().addTask(this);
}

//-------------------------------------------------------------------------------------	
bool SignalHandlers::ignoreSignal(int sigNum)
{
#if KBE_PLATFORM != PLATFORM_WIN32
	if (signal(sigNum, SIG_IGN) == SIG_ERR)
		return false;
#endif

	return true;
}

//-------------------------------------------------------------------------------------	
SignalHandler* SignalHandlers::addSignal(int sigNum, 
	SignalHandler* pSignalHandler, int flags)
{
	// ��������
	// SignalHandlerMap::iterator iter = singnalHandlerMap_.find(sigNum);
	// KBE_ASSERT(iter == singnalHandlerMap_.end());

	singnalHandlerMap_[sigNum] = pSignalHandler;

#if KBE_PLATFORM != PLATFORM_WIN32
	struct sigaction action;
	action.sa_handler = &signalHandler;
	sigfillset( &(action.sa_mask) );

	if (flags & SA_SIGINFO)
	{
		ERROR_MSG( "ServerApp::installSingnal: "
				"SA_SIGINFO is not supported, ignoring\n" );
		flags &= ~SA_SIGINFO;
	}

	action.sa_flags = flags;

	::sigaction( sigNum, &action, NULL );
#endif

	return pSignalHandler;
}
	
//-------------------------------------------------------------------------------------	
SignalHandler* SignalHandlers::delSignal(int sigNum)
{
	SignalHandlerMap::iterator iter = singnalHandlerMap_.find(sigNum);
	KBE_ASSERT(iter != singnalHandlerMap_.end());
	SignalHandler* pSignalHandler = iter->second;
	singnalHandlerMap_.erase(iter);
	return pSignalHandler;
}
	
//-------------------------------------------------------------------------------------	
void SignalHandlers::clear()
{
	singnalHandlerMap_.clear();
}

//-------------------------------------------------------------------------------------	
void SignalHandlers::onSignalled(int sigNum)
{
	// ��Ҫ�����ڴ�
	KBE_ASSERT(wpos_ != 0XFF);
	signalledArray_[wpos_++] = sigNum;
}

//-------------------------------------------------------------------------------------	
bool SignalHandlers::process()
{
	if (wpos_ == 0)
		return true;

	DEBUG_MSG(fmt::format("SignalHandlers::process: rpos={}, wpos={}.\n", rpos_, wpos_));

#if KBE_PLATFORM != PLATFORM_WIN32
	/* ����ź���˲ʱ����255�������󣬿��Դ�ע�ͣ��������������źŵ�ִ�����֮����ִ���ڼ䴥�����źţ���signalledArray_��Ϊ�źż�����
	if (wpos_ == 1 && signalledArray_[0] == SIGALRM)
		return true;

	sigset_t mask, old_mask;
	sigemptyset(&mask);
	sigemptyset(&old_mask);

	sigfillset(&mask);

	// �����ź�
	sigprocmask(SIG_BLOCK, &mask, &old_mask);
	*/
#endif

	while (rpos_ < wpos_)
	{
		int sigNum = signalledArray_[rpos_++];

#if KBE_PLATFORM != PLATFORM_WIN32
		//if (SIGALRM == sigNum)
		//	continue;
#endif

		SignalHandlerMap::iterator iter1 = singnalHandlerMap_.find(sigNum);
		if (iter1 == singnalHandlerMap_.end())
		{
			DEBUG_MSG(fmt::format("SignalHandlers::process: sigNum {} unhandled, singnalHandlerMap({}).\n",
				SIGNAL2NAMES(sigNum), singnalHandlerMap_.size()));

			continue;
		}

		DEBUG_MSG(fmt::format("SignalHandlers::process: sigNum {} handle. singnalHandlerMap({})\n", SIGNAL2NAMES(sigNum), singnalHandlerMap_.size()));
		iter1->second->onSignalled(sigNum);
	}

	rpos_ = 0;
	wpos_ = 0;

#if KBE_PLATFORM != PLATFORM_WIN32
	// �ָ�����
	/*
	sigprocmask(SIG_SETMASK, &old_mask, NULL);

	addSignal(SIGALRM, NULL);

	// Get the current signal mask
	sigprocmask(0, NULL, &mask);

	// Unblock SIGALRM
	sigdelset(&mask, SIGALRM);

	// Wait with this mask
	ualarm(1, 0);

	// ���ڼ������ź����´���
	sigsuspend(&mask);

	delSignal(SIGALRM);
	*/
#endif

	return true;
}

//-------------------------------------------------------------------------------------		
}
