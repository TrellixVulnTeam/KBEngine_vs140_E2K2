// Copyright 2008-2018 Yolo Technologies, Inc. All Rights Reserved. https://www.comblockengine.com

#ifndef KBE_TASK_H
#define KBE_TASK_H

#include "common/common.h"

namespace KBEngine
{

	//add by huyf 2019.03.06 ������ͼ
	/*
	����
																										class Task(�������):process
																												��
			��������������������������������������������������������������������������������������������������������������������������������������������������������������������������������������������������������������������������������������������������������������������������������������������������������������������������������������������������������������������������������������������������������������������������������������������������������������������������������������������������������������������������������������������������������������������������������������������������������������������������������������������������������������������������������������������������������������������������������������������������������������������������������������������������������������������������������������������������������������������������������������������������������������������������������������������������������������������������������������������������������������
			��								��							��					��					��								��									��								��						��						��								��													��								��							��										��						��							��
	class CProfileHandler	class DelayedChannels	class PyTickProfileHandler	class Components	class thread::TPTask	class ForwardComponent_MessageBuffer	class ForwardAnywhere_MessageBuffer	class PendingLoginMgr	class SignalHandlers	class InitProgressHandler	class EntityMessagesForwardCellappHandler	class BaseMessagesForwardClientHandler	class RestoreEntityHandler	class SyncEntityStreamTemplateHandler	class InitProgressHandler	class SyncAppDatasHandler	class ClientSDKDownloader
																												��
										������������������������������������������������������������������������������������������������������������������������������������������������������������������������������������������������������������������������������������������������������������������������������������������������������������������������������������������������
										��							��						��					��								��								��								��
							class LoadNavmeshTask	class DataDownload		class SendEMailTask		class DBTaskBase�������ࣩ	class InterfacesHandler_Interfaces		class FindServersTask				class LookAppTask
																												��
																							����������������������������������������������������������������������������������
																							��										��
																						class DBTaskSyncTable					class DBTask
																																	��
					����������������������������������������������������������������������������������������������������������������������������������������������������������������������������������������������������������������������������������������������������������������������������������������������������������������������������������������������������������������������������������������������������������������������������������������������������������������������������������������������������������������������������������������������������������������������������������������������������������������������������������������������������������������������������������������������������������������������������������������������������������������������������������������������������������
					��								��								��						��						��					��						��						��							��							��								��						��							��						��					��						��
				DBTaskExecuteRawDatabaseCommand	DBTaskDeleteEntityByDBID	 DBTaskEntityAutoLoad	DBTaskLookUpEntityByDBID	 EntityDBTask		DBTaskCreateAccount	 DBTaskCreateMailAccount	 DBTaskActivateAccount	 DBTaskReqAccountResetPassword	 DBTaskAccountResetPassword	 DBTaskReqAccountBindEmail	 DBTaskAccountBindEmail	 DBTaskAccountNewPassword	 DBTaskAccountLogin	 DBTaskServerLog	 DBTaskEraseBaseappEntityLog
																																	��
										����������������������������������������������������������������������������������������������������������������������������������������������������������������������������������������������������������������������������������������������������������������������������������������������������������������
										��								��								��			��						��					��								��
										DBTaskExecuteRawDatabaseCommandByEntity	 DBTaskWriteEntity		 DBTaskRemoveEntity	 DBTaskQueryAccount	 DBTaskAccountOnline	 DBTaskEntityOffline	 DBTaskQueryEntity



	*/

/**
 *	����һ������
 */
class Task
{
public:
	virtual ~Task() {}
	virtual bool process() = 0;
};


}

#endif // KBE_TASK_H
