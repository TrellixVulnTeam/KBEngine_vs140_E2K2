// Copyright 2008-2018 Yolo Technologies, Inc. All Rights Reserved. https://www.comblockengine.com

#include "baseapp.h"
#include "entity.h"
#include "profile.h"
#include "entity_messages_forward_handler.h"
#include "pyscript/py_gc.h"
#include "entitydef/entity_call.h"
#include "entitydef/entity_component.h"
#include "entitydef/entitydef.h"
#include "network/channel.h"	
#include "network/fixed_messages.h"
#include "client_lib/client_interface.h"
#include "common/sha1.h"

#ifndef CODE_INLINE
#include "entity.inl"
#endif

#include "../../server/baseapp/baseapp_interface.h"
#include "../../server/cellapp/cellapp_interface.h"
#include "../../server/dbmgr/dbmgr_interface.h"

namespace KBEngine{

SWALLOWER_OBJECT

ENTITY_METHOD_DECLARE_BEGIN(Baseapp, Entity)
SCRIPT_METHOD_DECLARE("createCellEntity",				createCellEntity,				METH_VARARGS,			0)
SCRIPT_METHOD_DECLARE("createCellEntityInNewSpace",		createCellEntityInNewSpace,		METH_VARARGS,			0)
SCRIPT_METHOD_DECLARE("destroyCellEntity",				pyDestroyCellEntity,			METH_VARARGS,			0)
SCRIPT_METHOD_DECLARE("teleport",						pyTeleport,						METH_VARARGS,			0)
SCRIPT_METHOD_DECLARE("getControllers",					pyGetControllers,				METH_VARARGS,			0)
ENTITY_METHOD_DECLARE_END()

SCRIPT_MEMBER_DECLARE_BEGIN(Entity)
SCRIPT_MEMBER_DECLARE_END()

ENTITY_GETSET_DECLARE_BEGIN(Entity)
SCRIPT_GET_DECLARE("cell",								pyGetCellEntityCall,			0,								0)	
SCRIPT_GET_DECLARE("client",							pyGetClientEntityCall,			0,								0)	
SCRIPT_GET_DECLARE("databaseID",						pyGetDBID,						0,								0)	
SCRIPT_GET_DECLARE("databaseInterfaceName",				pyGetDBInterfaceName,			0,								0)
SCRIPT_GETSET_DECLARE("shouldAutoBackup",				pyGetShouldAutoBackup,			pySetShouldAutoBackup,			0,		0)
SCRIPT_GETSET_DECLARE("shouldAutoArchive",				pyGetShouldAutoArchive,			pySetShouldAutoArchive,			0,		0)
ENTITY_GETSET_DECLARE_END()
BASE_SCRIPT_INIT(Entity, 0, 0, 0, 0, 0)

//-------------------------------------------------------------------------------------
Entity::Entity(ENTITY_ID id, const ScriptDefModule* pScriptModule, 
		   PyTypeObject* pyType, bool isInitialised):
ScriptObject(pyType, isInitialised),
ENTITY_CONSTRUCTION(Entity),
clientEntityCall_(NULL),
cellEntityCall_(NULL),
cellDataDict_(NULL),
hasDB_(false),
DBID_(0),
isGetingCellData_(false),
isArchiveing_(false),
shouldAutoArchive_(1),
shouldAutoBackup_(1),
creatingCell_(false),
createdSpace_(false),
inRestore_(false),
pBufferedSendToClientMessages_(NULL),
dbInterfaceIndex_(0)
{
	setDirty();

	script::PyGC::incTracing("Entity");
	ENTITY_INIT_PROPERTYS(Entity);

	// 创建并初始化cellData
	createCellData();
}

//-------------------------------------------------------------------------------------
Entity::~Entity()
{
	DEBUG_MSG(fmt::format("huyf-yh: Entity::~Entity-0 id_={}\n", id()));
	ENTITY_DECONSTRUCTION(Entity);
	S_RELEASE(clientEntityCall_);
	S_RELEASE(cellEntityCall_);
	S_RELEASE(cellDataDict_);
	SAFE_RELEASE(pBufferedSendToClientMessages_);

	DEBUG_MSG(fmt::format("huyf-yh: Entity::~Entity id_={}\n", id()));
	if(Baseapp::getSingleton().pEntities())
		Baseapp::getSingleton().pEntities()->pGetbages()->erase(id());

	script::PyGC::decTracing("Entity");
}	

//-------------------------------------------------------------------------------------
void Entity::onInitializeScript()
{
	DEBUG_MSG(fmt::format("{}::onInitializeScript \n", pScriptModule_->getName()));
// 	pPropertyDescrs_ = &pScriptModule_->getPropertyDescrs();
// 	ScriptDefModule::PROPERTYDESCRIPTION_MAP::const_iterator iter = pPropertyDescrs_->find("compIdCountDict");	
// 	if (iter != pPropertyDescrs_->end())
// 	{
// 		PropertyDescription* propertyDescription = iter->second;
// 		DataType* dataType = propertyDescription->getDataType();
// 
// 	}
}

//-------------------------------------------------------------------------------------
void Entity::onDefDataChanged(EntityComponent* pEntityComponent, const PropertyDescription* propertyDescription,
		PyObject* pyData)
{
	if(initing())
		return;

	if(propertyDescription->isPersistent())
		setDirty();
	
	uint32 flags = propertyDescription->getFlags();
	ENTITY_PROPERTY_UID componentPropertyUID = 0;
	int8 componentPropertyAliasID = 0;

	if (pEntityComponent)
	{
		PropertyDescription* pComponentPropertyDescription = pEntityComponent->pPropertyDescription();

		if (pComponentPropertyDescription)
		{
			componentPropertyUID = pComponentPropertyDescription->getUType();
			componentPropertyAliasID = pComponentPropertyDescription->aliasIDAsUint8();
		}
		else
		{
			ERROR_MSG(fmt::format("{}::onDefDataChanged: EntityComponent({}) not found pComponentPropertyDescription!\n",
				pScriptModule_->getName(), pEntityComponent->pComponentScriptDefModuleDescrs()->getName()));

			KBE_ASSERT(false);
			return;
		}
	}

	if((flags & ED_FLAG_BASE_AND_CLIENT) <= 0 || clientEntityCall_ == NULL)
		return;

	// 创建一个需要广播的模板流
	MemoryStream* mstream = MemoryStream::createPoolObject(OBJECTPOOL_POINT);

	propertyDescription->getDataType()->addToStream(mstream, pyData);

	Network::Bundle* pBundle = Network::Bundle::createPoolObject(OBJECTPOOL_POINT);
	(*pBundle).newMessage(ClientInterface::onUpdatePropertys);
	(*pBundle) << id();

	if (pScriptModule_->usePropertyDescrAlias())
	{
		(*pBundle) << componentPropertyAliasID;
		(*pBundle) << propertyDescription->aliasIDAsUint8();
	}
	else
	{
		(*pBundle) << componentPropertyUID;
		(*pBundle) << propertyDescription->getUType();
	}

	pBundle->append(*mstream);
	
	g_privateClientEventHistoryStats.trackEvent(scriptName(), 
		propertyDescription->getName(), 
		pBundle->currMsgLength());

	// 按照当前的设计来说，有clientEntityCall_必定是proxy
	// 至于为何跑到baseEntity里来和python本身是C语言实现有关
	static_cast<Proxy*>(this)->sendToClient(ClientInterface::onUpdatePropertys, pBundle);
	MemoryStream::reclaimPoolObject(mstream);
}

//-------------------------------------------------------------------------------------
void Entity::onDestroy(bool callScript)
{
	if(callScript)
	{
		SCOPED_PROFILE(SCRIPTCALL_PROFILE);
		CALL_ENTITY_AND_COMPONENTS_METHOD(this, SCRIPT_OBJECT_CALL_ARGS0(pyTempObj, const_cast<char*>("onDestroy"), GETERR));
	}

	if(this->hasDB())
	{
		onCellWriteToDBCompleted(0, -1, -1);
	}
	
	eraseEntityLog();

	// 按照当前的设计来说，有clientEntityCall_必定是proxy
	// 至于为何跑到baseEntity里来和python本身是C语言实现有关
	if(clientEntityCall_)
		static_cast<Proxy*>(this)->kick();
}

//-------------------------------------------------------------------------------------
void Entity::eraseEntityLog()
{
	// 这里没有使用hasDB()来进行判断
	// 用户可能destroy( writeToDB = False ), 这个操作会导致hasDB为false， 因此这里
	// 需要判断dbid是否大于0， 如果大于0则应该要去擦除在线等记录情况.
	if(this->dbid() > 0)
	{
		Network::Bundle* pBundle = Network::Bundle::createPoolObject(OBJECTPOOL_POINT);
		(*pBundle).newMessage(DbmgrInterface::onEntityOffline);
		(*pBundle) << this->dbid();
		(*pBundle) << this->pScriptModule()->getUType();
		(*pBundle) << dbInterfaceIndex();

		Components::COMPONENTS& cts = Components::getSingleton().getComponents(DBMGR_TYPE);
		Components::ComponentInfos* dbmgrinfos = NULL;

		if(cts.size() > 0)
			dbmgrinfos = &(*cts.begin());

		if(dbmgrinfos == NULL || dbmgrinfos->pChannel == NULL || dbmgrinfos->cid == 0)
		{
			ERROR_MSG("Entity::onDestroy: not found dbmgr!\n");
			Network::Bundle::reclaimPoolObject(pBundle);
			return;
		}

		dbmgrinfos->pChannel->send(pBundle);
	}
}

//-------------------------------------------------------------------------------------
bool Entity::installCellDataAttr(PyObject* dictData, bool installpy)
{
	if(dictData != NULL)
	{
		if(cellDataDict_ != dictData)
		{
			if(cellDataDict_ != NULL)
				Py_DECREF(cellDataDict_);

			cellDataDict_ = dictData;
			Py_INCREF(cellDataDict_);
		}
	}

	if(installpy)
	{
		if(cellDataDict_ == NULL)
		{
			cellDataDict_ = PyDict_New();
		}

		if(PyObject_SetAttrString(this, "cellData", cellDataDict_) == -1)
		{
			ERROR_MSG("Entity::installCellDataAttr: set property cellData error!\n");
			SCRIPT_ERROR_CHECK();
			return false;
		}
	}

	return true;
}

//-------------------------------------------------------------------------------------
void Entity::createCellData(void)
{
	if(!pScriptModule_->hasCell() || !installCellDataAttr())
	{
		if(pScriptModule_->getCellPropertyDescriptions().size() > 0)
		{
			if(!pScriptModule_->hasCell())
			{
				WARNING_MSG(fmt::format("{}::createCellData: do not create cellData, cannot find the cellapp script({})!\n", 
					pScriptModule_->getName(), pScriptModule_->getName()));
			}
		}

		return;
	}
	
	EntityDef::context().currComponentType = CELLAPP_TYPE;

	ScriptDefModule::PROPERTYDESCRIPTION_MAP& propertyDescrs = pScriptModule_->getCellPropertyDescriptions();
	ScriptDefModule::PROPERTYDESCRIPTION_MAP::const_iterator iter = propertyDescrs.begin();
	for(; iter != propertyDescrs.end(); ++iter)
	{
		PropertyDescription* propertyDescription = iter->second;
		DataType* dataType = propertyDescription->getDataType();
		
		if(dataType)
		{
			PyObject* pyObj = NULL;
			
			if (dataType->type() != DATA_TYPE_ENTITY_COMPONENT)
			{
				//DEBUG_MSG(fmt::format("huyf-yh: {}::createCellData: type=Not ComponentType key={} set newDefaultVal in cellDataDict_.\n", this->scriptName(), propertyDescription->getName()));
				pyObj = propertyDescription->newDefaultVal();
			}
			else
			{
				//DEBUG_MSG(fmt::format("huyf-yh: {}::createCellData: type=ComponentType key={} set newDefaultVal in cellDataDict_.\n", this->scriptName(), propertyDescription->getName()));
				pyObj = ((EntityComponentType*)dataType)->createCellData();
			}
			PyDict_SetItemString(cellDataDict_, propertyDescription->getName(), pyObj);
			Py_DECREF(pyObj);
		}
		else
		{
			ERROR_MSG(fmt::format("{}::createCellData: {} PropertyDescription the dataType is NULL.\n",
				this->scriptName(), propertyDescription->getName()));	
		}
		
		SCRIPT_ERROR_CHECK();
	}

	// 初始化cellEntity的位置和方向变量
	PyObject* position = PyTuple_New(3);
	PyTuple_SET_ITEM(position, 0, PyFloat_FromDouble(0.0));
	PyTuple_SET_ITEM(position, 1, PyFloat_FromDouble(0.0));
	PyTuple_SET_ITEM(position, 2, PyFloat_FromDouble(0.0));
	
	PyObject* direction = PyTuple_New(3);
	PyTuple_SET_ITEM(direction, 0, PyFloat_FromDouble(0.0));
	PyTuple_SET_ITEM(direction, 1, PyFloat_FromDouble(0.0));
	PyTuple_SET_ITEM(direction, 2, PyFloat_FromDouble(0.0));
	
	PyDict_SetItemString(cellDataDict_, "position", position);
	PyDict_SetItemString(cellDataDict_, "direction", direction);

	Py_DECREF(position);
	Py_DECREF(direction);

	SCRIPT_ERROR_CHECK();
}

//-------------------------------------------------------------------------------------
//add by huyf 2019.09.09
/*
* 创建实体(base)部分后，base进程再把实体的celldata中的属性发送给cell进程(默认的属性修改为不再发送)
* cell进程收到后，进行数据解析，进行cell进程实体对象的创建。
* 发生在实体创建的时候：createCellEntityInNewSpace、restoreSpaceInCell、createCellEntity
*/
//add end
void Entity::addCellDataToStream(COMPONENT_TYPE sendTo, uint32 flags, MemoryStream* s, bool useAliasID)
{
	addPositionAndDirectionToStream(*s, useAliasID);

	if (!cellDataDict_)
		return;

	if(sendTo != CLIENT_TYPE)
		EntityDef::context().currComponentType = CELLAPP_TYPE;
	else
		EntityDef::context().currComponentType = CLIENT_TYPE;

	int nDefSameCount = 0;
	int nNotDefSameCount = 0;

	ScriptDefModule::PROPERTYDESCRIPTION_MAP& propertyDescrs = pScriptModule_->getCellPropertyDescriptions();
	ScriptDefModule::PROPERTYDESCRIPTION_MAP::const_iterator iter = propertyDescrs.begin();

	for(; iter != propertyDescrs.end(); ++iter)
	{
		PropertyDescription* propertyDescription = iter->second;
		if(flags == 0 || (flags & propertyDescription->getFlags()) > 0)
		{
			PyObject* pyVal = PyDict_GetItemString(cellDataDict_, propertyDescription->getName());

			if (propertyDescription->getDataType()->type() == DATA_TYPE_ENTITY_COMPONENT)
			{
				// 由于存在一种情况， 组件def中没有内容， 但有cell脚本，此时baseapp上无法判断他是否有cell属性，所以写celldata时没有数据写入
				EntityComponentType* pEntityComponentType = (EntityComponentType*)propertyDescription->getDataType();
				if (pEntityComponentType->pScriptDefModule()->getCellPropertyDescriptions().size() == 0)
					continue;

				if (useAliasID && pScriptModule_->usePropertyDescrAlias())
				{
					(*s) << (uint8)0;
					(*s) << propertyDescription->aliasIDAsUint8();
				}
				else
				{
					(*s) << (ENTITY_PROPERTY_UID)0;
					(*s) << propertyDescription->getUType();
				}

				pEntityComponentType->addCellDataToStream(s, flags, pyVal, this->id(), propertyDescription, sendTo, true);
			}
			else
			{

				//add by huyf 2019.06.13:实体数据序列化优化
				if (propertyDescription->isSameDefaultValue(pyVal))
				{
					//nDefSameCount++;
					//WARNING_MSG(fmt::format("huyf-yh: scriptName=[{}] is same default value nDefSameCount=[{}] UType=[ {} ] scriptName=[ {} ] propertyName=[ {} ] tp_name=[ {} ] is DataTypeName=[ {} ]!\n",
					//	this->scriptName(), nDefSameCount, propertyDescription->getUType(), this->scriptName(), propertyDescription->getName(), (pyVal ? pyVal->ob_type->tp_name : "unknown"), propertyDescription->getDataType()->getName()));
					continue;
				}

				nNotDefSameCount++;
				//WARNING_MSG(fmt::format("huyf-yh: scriptName=[{}] isn't same default value nNotDefSameCount=[{}] UType=[ {} ] scriptName=[ {} ]  propertyName=[ {} ] tp_name=[ {} ] is DataTypeName=[ {} ]!\n",
				//	this->scriptName(), nNotDefSameCount, propertyDescription->getUType(), this->scriptName(), propertyDescription->getName(), (pyVal ? pyVal->ob_type->tp_name : "unknown"), propertyDescription->getDataType()->getName()));
				//add end:实体数据序列化优化

				if (useAliasID && pScriptModule_->usePropertyDescrAlias())
				{
					(*s) << (uint8)0;
					(*s) << propertyDescription->aliasIDAsUint8();
				}
				else
				{
					(*s) << (ENTITY_PROPERTY_UID)0;
					(*s) << propertyDescription->getUType();
				}

				if (!propertyDescription->isSameType(pyVal))
				{
					ERROR_MSG(fmt::format("{}::addCellDataToStream: {}({}) not is ({})!\n", this->scriptName(),
						propertyDescription->getName(), (pyVal ? pyVal->ob_type->tp_name : "unknown"), propertyDescription->getDataType()->getName()));

					PyObject* pydefval = propertyDescription->parseDefaultStr("");
					propertyDescription->addToStream(s, pydefval);
					Py_DECREF(pydefval);
				}
				else
				{
					propertyDescription->addToStream(s, pyVal);
				}
			}

			if (PyErr_Occurred())
 			{	
				PyErr_PrintEx(0);
				DEBUG_MSG(fmt::format("{}::addCellDataToStream: {} error!\n", this->scriptName(),
					propertyDescription->getName()));
			}
		}
	}
}

//-------------------------------------------------------------------------------------
//add by huyf 2019.06.13:在玩家数据写数据库的时候，base进程会请求cell进程的数据（cell进程会把所有的属性都通过网络发送到base进程。）
//base进程收到后在对所有的属性进行判定是否为持久化属性，在进行数据库的存储操作。
//可以优化：
//1、非持久化属性不用同步到base进程。现阶段可以支持的
//2、没有变脏的属性不用同步到base进程。需要扩展PropertyDescription类，增加"脏"字段标识
void Entity::addPersistentsDataToStream(uint32 flags, MemoryStream* s)
{
	WARNING_MSG(fmt::format("huyf-yh: scriptName={}::addPersistentsDataToStream: id={} is enter flags=({})\n",
		this->scriptName(), this->id(), flags));

	std::vector<ENTITY_PROPERTY_UID> log;

	// 再将base中存储属性取出
	PyObject* pydict = PyObject_GetAttrString(this, "__dict__");

	// 先将celldata中的存储属性取出
	ScriptDefModule::PROPERTYDESCRIPTION_MAP& propertyDescrs = pScriptModule_->getPersistentPropertyDescriptions();
	ScriptDefModule::PROPERTYDESCRIPTION_MAP::const_iterator iter = propertyDescrs.begin();

	if(pScriptModule_->hasCell())
	{
		addPositionAndDirectionToStream(*s);
	}

	for(; iter != propertyDescrs.end(); ++iter)
	{
		PropertyDescription* propertyDescription = iter->second;
		std::vector<ENTITY_PROPERTY_UID>::const_iterator finditer = 
			std::find(log.begin(), log.end(), propertyDescription->getUType());

		if(finditer != log.end())
			continue;

		const char* attrname = propertyDescription->getName();
		if(propertyDescription->isPersistent() && (flags & propertyDescription->getFlags()) > 0)
		{
			bool isComponent = propertyDescription->getDataType()->type() == DATA_TYPE_ENTITY_COMPONENT;

			PyObject *key = PyUnicode_FromString(attrname);
			//add by huyf 2019.06.20
			/*if (cellDataDict_ != NULL)
			{
				DEBUG_MSG(fmt::format("huyf-yh: {}::addPersistentsDataToStream-0: id={} isComponent:{} attrname:{} cellDataDict_Contains:{}.\n",
					this->scriptName(), this->id(), isComponent, attrname, PyDict_Contains(cellDataDict_, key)));
			}
			else
			{
				DEBUG_MSG(fmt::format("huyf-yh: {}::addPersistentsDataToStream-0: id={} isComponent:{} attrname:{} cellDataDict_Contains:{}.\n",
					this->scriptName(), this->id(), isComponent, attrname, "cellDataDict_==NULL"));
			}*/
			//add end

			if(!isComponent /* 如果是组件类型，应该先从实体自身找到这个组件属性 */
				&& cellDataDict_ != NULL && PyDict_Contains(cellDataDict_, key) > 0)
			{
				PyObject* pyVal = PyDict_GetItemString(cellDataDict_, attrname);
				if(!propertyDescription->isSamePersistentType(pyVal))
				{
					CRITICAL_MSG(fmt::format("{}::addPersistentsDataToStream: {} persistent({}) type(curr_py: {} != {}) error.\n",
						this->scriptName(), this->id(), attrname, (pyVal ? pyVal->ob_type->tp_name : "unknown"), propertyDescription->getDataType()->getName()));
				}
				else
				{
					//add by huyf 2019.09.06:解决S_Return(Py_None)覆盖initProperty->createNamespace的问题
					//持久化属性，必须设置默认值后插入数据库，否则不插入的话，数据库里面为NULL，读取出来的话会是py_None
// 					if (propertyDescription->isSameDefaultValue(pyVal))
// 						continue;
					//add end:解决S_Return(Py_None)覆盖initProperty->createNamespace的问题

					(*s) << (ENTITY_PROPERTY_UID)0 << propertyDescription->getUType();
					log.push_back(propertyDescription->getUType());
					propertyDescription->addPersistentToStream(s, pyVal);
					DEBUG_PERSISTENT_PROPERTY("addCellPersistentsDataToStream", attrname);
					//add by huyf 2019.06.20
					//DEBUG_MSG(fmt::format("huyf-yh: {}::addPersistentsDataToStream-1: {} persistent({}) type(curr_py: {} == {}) is suc.\n",
					//	this->scriptName(), this->id(), attrname, (pyVal ? pyVal->ob_type->tp_name : "unknown"), propertyDescription->getDataType()->getName()));
					//add end
				}
			}
			else if(PyDict_Contains(pydict, key) > 0)	//add by huyf 2019.06.20:accountName - BASE - Persistent
			{
				PyObject* pyVal = PyDict_GetItem(pydict, key);
				if(!propertyDescription->isSamePersistentType(pyVal))
				{
					CRITICAL_MSG(fmt::format("{}::addPersistentsDataToStream: {} persistent({}) type(curr_py: {} != {}) error.\n",
						this->scriptName(), this->id(), attrname, (pyVal ? pyVal->ob_type->tp_name : "unknown"), propertyDescription->getDataType()->getName()));
				}
				else
				{
					//add by huyf 2019.09.06:解决S_Return(Py_None)覆盖initProperty->createNamespace的问题
					//持久化属性，必须设置默认值后插入数据库，否则不插入的话，数据库里面为NULL，读取出来的话会是py_None
// 					if (propertyDescription->isSameDefaultValue(pyVal))
// 						continue;
					//add end:解决S_Return(Py_None)覆盖initProperty->createNamespace的问题

	    			(*s) << (ENTITY_PROPERTY_UID)0 << propertyDescription->getUType();
					log.push_back(propertyDescription->getUType());
	    			propertyDescription->addPersistentToStream(s, pyVal);
					DEBUG_PERSISTENT_PROPERTY("addBasePersistentsDataToStream", attrname);
					//add by huyf 2019.06.20
					//DEBUG_MSG(fmt::format("huyf-yh: {}::addPersistentsDataToStream-2: {} persistent({}) type(curr_py: {} == {}) is suc.\n",
					//	this->scriptName(), this->id(), attrname, (pyVal ? pyVal->ob_type->tp_name : "unknown"), propertyDescription->getDataType()->getName()));
					//add end
				}
			}
			else
			{
				if (!isComponent)
				{
					WARNING_MSG(fmt::format("{}::addPersistentsDataToStream: {} not found Persistent({}), use default values!\n",
						this->scriptName(), this->id(), attrname));

					(*s) << (ENTITY_PROPERTY_UID)0 << propertyDescription->getUType();
					log.push_back(propertyDescription->getUType());
					propertyDescription->addPersistentToStream(s, NULL);
				}
				else
				{
					// 一些实体没有cell部分， 因此cell属性忽略
					if (cellDataDict_)
					{
						// 一些组件可能没有cell属性
						EntityComponentType* pEntityComponentType = (EntityComponentType*)propertyDescription->getDataType();
						if (pEntityComponentType->pScriptDefModule()->getCellPropertyDescriptions().size() == 0)
							continue;

						PyObject* pyVal = PyDict_GetItemString(cellDataDict_, attrname);
						if (!propertyDescription->isSamePersistentType(pyVal))
						{
							CRITICAL_MSG(fmt::format("{}::addPersistentsDataToStream: {} persistent({}) type(curr_py: {} != {}) error.\n",
								this->scriptName(), this->id(), attrname, (pyVal ? pyVal->ob_type->tp_name : "unknown"), propertyDescription->getDataType()->getName()));
						}
						else
						{
							(*s) << (ENTITY_PROPERTY_UID)0 << propertyDescription->getUType();
							log.push_back(propertyDescription->getUType());
							propertyDescription->addPersistentToStream(s, pyVal);
							DEBUG_PERSISTENT_PROPERTY("addCellPersistentsDataToStream", attrname);
							//add by huyf 2019.06.20
							//DEBUG_MSG(fmt::format("huyf-yh: {}::addPersistentsDataToStream-3: {} persistent({}) type(curr_py: {} == {}) is suc.\n",
							//	this->scriptName(), this->id(), attrname, (pyVal ? pyVal->ob_type->tp_name : "unknown"), propertyDescription->getDataType()->getName()));
							//add end
						}
					}
				}
			}

			Py_DECREF(key);
		}

		SCRIPT_ERROR_CHECK();
	}

	Py_XDECREF(pydict);
	SCRIPT_ERROR_CHECK();
}

//-------------------------------------------------------------------------------------
PyObject* Entity::createCellDataDict(uint32 flags)
{
	PyObject* cellData = PyDict_New();

	ScriptDefModule::PROPERTYDESCRIPTION_MAP& propertyDescrs = pScriptModule_->getCellPropertyDescriptions();
	ScriptDefModule::PROPERTYDESCRIPTION_MAP::const_iterator iter = propertyDescrs.begin();
	for(; iter != propertyDescrs.end(); ++iter)
	{
		PropertyDescription* propertyDescription = iter->second;
		if((flags & propertyDescription->getFlags()) > 0)
		{
			PyObject* pyVal = PyDict_GetItemString(cellDataDict_, propertyDescription->getName());
			PyDict_SetItemString(cellData, propertyDescription->getName(), pyVal);
			Py_DECREF(pyVal);
			SCRIPT_ERROR_CHECK();
		}
	}

	return cellData;
}

//-------------------------------------------------------------------------------------
void Entity::sendToCellapp(Network::Bundle* pBundle)
{
	if (!cellEntityCall_)
	{
		ERROR_MSG(fmt::format("{}::sendToCellapp: no cell! entityID={}\n", this->scriptName(), id()));
		return;
	}

	sendToCellapp(cellEntityCall_->getChannel(), pBundle);
}

//-------------------------------------------------------------------------------------
void Entity::sendToCellapp(Network::Channel* pChannel, Network::Bundle* pBundle)
{
	if (!pChannel)
	{
		ERROR_MSG(fmt::format("{}::sendToCellapp: pChannel == NULL! entityID={}\n", this->scriptName(), id()));
		return;
	}

	KBE_ASSERT(pBundle != NULL);

	pChannel->send(pBundle);
}

//-------------------------------------------------------------------------------------
void Entity::destroyCellData(void)
{
	// cellDataDict_ 继续保留， 以供备份时使用， 这里仅仅让脚步层无法访问到即可
	// S_RELEASE(cellDataDict_);
	if(PyObject_DelAttrString(this, "cellData") == -1)
	{
		ERROR_MSG(fmt::format("{}::destroyCellData: delete cellData error!\n", this->scriptName()));
		SCRIPT_ERROR_CHECK();
	}
}

//-------------------------------------------------------------------------------------
//add by huyf 2019.07.08:实体销毁-2
bool Entity::destroyCellEntity(void)
{
	DEBUG_MSG(fmt::format("huyf-yh: Entity::destroyCellEntity id_={}\n", id_));
	if(isDestroyed())	
	{
		return false;																					
	}

	if(cellEntityCall_  == NULL || cellEntityCall_->getChannel() == NULL)
	{
		isArchiveing_ = false;
		return false;
	}

	Network::Bundle* pBundle = Network::Bundle::createPoolObject(OBJECTPOOL_POINT);
	(*pBundle).newMessage(CellappInterface::onDestroyCellEntityFromBaseapp);
	(*pBundle) << id_;
	sendToCellapp(pBundle);
	return true;
}

//-------------------------------------------------------------------------------------
//add by huyf 2019.07.08:实体销毁-1 entity请求销毁cell进程中的实体：SCRIPT_METHOD_DECLARE("destroyCellEntity",				pyDestroyCellEntity,			METH_VARARGS,			0)
PyObject* Entity::pyDestroyCellEntity()
{
	if(cellEntityCall_ == NULL) 
	{
		PyErr_Format(PyExc_AssertionError, "%s::destroyCellEntity: id:%i no cell! creatingCell=%s\n", 
			this->scriptName(), this->id(),
			creatingCell_ ? "true" : "false");
		PyErr_PrintEx(0);
		return 0;
	}
	else
		destroyCellEntity();

	S_Return;
}

//-------------------------------------------------------------------------------------
//add by huyf 2019.07.08:实体销毁-10
PyObject* Entity::__py_pyDestroyEntity(PyObject* self, PyObject* args, PyObject * kwargs)
{
	Entity* pobj = static_cast<Entity*>(self);
	DEBUG_MSG(fmt::format("{}::__py_pyDestroyEntity(): id={} .\n", pobj->scriptName(), pobj->id()));

	if(pobj->initing())
	{
		PyErr_Format(PyExc_AssertionError,
			"%s::destroy(): %d initing, reject the request!\n",	
			pobj->scriptName(), pobj->id());
		PyErr_PrintEx(0);
		return NULL;
	}

	static char * keywords[] =
	{
		const_cast<char *> ("deleteFromDB"),
		const_cast<char *> ("writeToDB"),
		NULL
	};

	if(pobj->isDestroyed())	
	{
		PyErr_Format(PyExc_AssertionError, "%s::destroy: %d is destroyed!\n",
			pobj->scriptName(), pobj->id());

		PyErr_PrintEx(0);
		return NULL;
	}

	if (pobj->creatingCell())
	{
		WARNING_MSG(fmt::format("{}::destroy(): id={} creating cell! automatic 'destroy' process will begin after 'onGetCell'.\n", 
			pobj->scriptName(), pobj->id()));

		pobj->addFlags(ENTITY_FLAGS_DESTROY_AFTER_GETCELL);
		S_Return;
	}

	if (pobj->cellEntityCall() != NULL)
	{
		PyErr_Format(PyExc_Exception, "%s::destroy: id:%i has cell, please destroyCellEntity() first!\n",
			pobj->scriptName(), pobj->id());

		PyErr_PrintEx(0);
		return NULL;
	}

	PyObject* pyDeleteFromDB = NULL;
	PyObject* pyWriteToDB = NULL;

	if (!PyArg_ParseTupleAndKeywords(args, kwargs, "|OO", 
		keywords, &pyDeleteFromDB, &pyWriteToDB))
	{
		PyErr_Format(PyExc_AssertionError, "%s::destroy: %d ParseTupleAndKeywords(deleteFromDB, &writeToDB) error!\n",
			pobj->scriptName(), pobj->id());

		PyErr_PrintEx(0);
		return NULL;
	}

	bool deleteFromDB = (pyDeleteFromDB != NULL) ? 
		(PyObject_IsTrue(pyDeleteFromDB) ? true : false) : false; 

	bool writeToDB = (pyWriteToDB != NULL) ? 
		(PyObject_IsTrue(pyWriteToDB) ? true : false) : pobj->hasDB();

	if(deleteFromDB || writeToDB)
	{
		// 有可能已经请求了writeToDB但还未返回写入的dbid
		// 这种情况需要返回给用户一个错误， 用户可以继续尝试这个操作
		if(pobj->hasDB() && pobj->dbid() == 0)
		{
			PyErr_Format(PyExc_AssertionError, "%s::destroy: id:%i has db, current dbid is 0. "
				"please wait for dbmgr to processing!\n", 
				pobj->scriptName(), pobj->id());

			PyErr_PrintEx(0);
			return NULL;
		}
	}

	pobj->onDestroyEntity(deleteFromDB, writeToDB);
	pobj->destroyEntity();

	S_Return;
}

//-------------------------------------------------------------------------------------
//add by huyf 2019.07.08:实体销毁-11
void Entity::onDestroyEntity(bool deleteFromDB, bool writeToDB)
{
	DEBUG_MSG(fmt::format("{}::onDestroyEntity(): id={} deleteFromDB={} writeToDB={}.\n", this->scriptName(), this->id(), deleteFromDB, writeToDB));

	if(deleteFromDB && hasDB())
	{
		Components::COMPONENTS& cts = Components::getSingleton().getComponents(DBMGR_TYPE);
		Components::ComponentInfos* dbmgrinfos = NULL;

		if(cts.size() > 0)
			dbmgrinfos = &(*cts.begin());

		if(dbmgrinfos == NULL || dbmgrinfos->pChannel == NULL || dbmgrinfos->cid == 0)
		{
			ERROR_MSG(fmt::format("{}::onDestroyEntity({}): writeToDB not found dbmgr!\n", this->scriptName(), this->id()));
			return;
		}

		Network::Bundle* pBundle = Network::Bundle::createPoolObject(OBJECTPOOL_POINT);
		(*pBundle).newMessage(DbmgrInterface::removeEntity);
		
		(*pBundle) << this->dbInterfaceIndex();
		(*pBundle) << g_componentID;
		(*pBundle) << this->id();
		(*pBundle) << this->dbid();
		(*pBundle) << this->pScriptModule()->getUType();
		dbmgrinfos->pChannel->send(pBundle);

		this->hasDB(false);
		return;
	}

	if(writeToDB)
	{
		// 这个行为默认会处理
		// this->writeToDB(NULL);
	}
	else
	{
		this->hasDB(false);
	}

	shouldAutoArchive_ = 0;
	shouldAutoBackup_ = 0;
}

//-------------------------------------------------------------------------------------
PyObject* Entity::onScriptGetAttribute(PyObject* attr)
{	
	DEBUG_OP_ATTRIBUTE("get", attr)
	PyObject* p = ScriptObject::onScriptGetAttribute(attr);
	//add by huyf 2019.09.05
	char* ccattr = PyUnicode_AsUTF8AndSize(attr, NULL);
	if (strcmp(ccattr, "compIdCountDict") == 0
		|| strcmp(ccattr, "permissionGroups") == 0
		|| strcmp(ccattr, "guildDuration") == 0)
	{
		DEBUG_MSG(fmt::format("{}::onScriptGetAttribute: ccattr={} tp_name={}.\n", scriptName(), ccattr, p->ob_type->tp_name));
	}
	//add end
	return p;
}	

//-------------------------------------------------------------------------------------
PyObject* Entity::pyGetCellEntityCall()
{ 
	if (!hasFlags(ENTITY_FLAGS_DESTROYING) && isDestroyed())
	{
		//add by huyf : S_ERR    baseapp01 422 4227001  [2019-08-13 18:07:41 887] - AssertionError: CNpc: 5326 is destroyed!
		PyErr_Format(PyExc_AssertionError, "%s: %d is destroyed!\n",		
			scriptName(), id());		
		PyErr_PrintEx(0);
		return 0;																					
	}

	EntityCall* entityCall = cellEntityCall();
	if (entityCall == NULL)
	{
		Py_INCREF(swallower);
		return swallower;
	}
		

	Py_INCREF(entityCall);
	return entityCall;
}

//-------------------------------------------------------------------------------------
PyObject* Entity::pyGetDBID()
{
	if (!hasFlags(ENTITY_FLAGS_DESTROYING) && isDestroyed())
	{
		PyErr_Format(PyExc_AssertionError, "%s: %d is destroyed!\n",		
			scriptName(), id());		
		PyErr_PrintEx(0);
		return 0;																					
	}

	return PyLong_FromUnsignedLongLong(this->dbid()); 
}

//-------------------------------------------------------------------------------------
PyObject* Entity::pyGetDBInterfaceName()
{
	if (!hasFlags(ENTITY_FLAGS_DESTROYING) && isDestroyed())
	{
		PyErr_Format(PyExc_AssertionError, "%s: %d is destroyed!\n",
			scriptName(), id());
		PyErr_PrintEx(0);
		return 0;
	}

	if (dbid() == 0)
		return PyUnicode_FromString("");

	return PyUnicode_FromString(g_kbeSrvConfig.dbInterfaceIndex2dbInterfaceName(dbInterfaceIndex()));
}

//-------------------------------------------------------------------------------------
PyObject* Entity::pyGetClientEntityCall()
{
	if (!hasFlags(ENTITY_FLAGS_DESTROYING) && isDestroyed())
	{
		PyErr_Format(PyExc_AssertionError, "%s: %d is destroyed!\n",
			scriptName(), id());
		PyErr_PrintEx(0);
		return 0;
	}

	EntityCall* entityCall = clientEntityCall();
	if (entityCall == NULL)
	{
		Py_INCREF(swallower);
		return swallower;
	}

	Py_INCREF(entityCall);
	return entityCall; 
}

//-------------------------------------------------------------------------------------
int Entity::pySetShouldAutoArchive(PyObject *value)
{
	if(isDestroyed())	
	{
		PyErr_Format(PyExc_AssertionError, "%s: %d is destroyed!\n",		
			scriptName(), id());		
		PyErr_PrintEx(0);
		return 0;																				
	}

	if(!PyLong_Check(value))
	{
		PyErr_Format(PyExc_AssertionError, "%s: %d set shouldAutoArchive value is not int!\n",		
			scriptName(), id());		
		PyErr_PrintEx(0);
		return 0;	
	}

	shouldAutoArchive_ = (int8)PyLong_AsLong(value);
	return 0;
}

//-------------------------------------------------------------------------------------
PyObject* Entity::pyGetShouldAutoArchive()
{
	return PyLong_FromLong(shouldAutoArchive_);
}

//-------------------------------------------------------------------------------------
int Entity::pySetShouldAutoBackup(PyObject *value)
{
	if(isDestroyed())	
	{
		PyErr_Format(PyExc_AssertionError, "%s: %d is destroyed!\n",		
			scriptName(), id());		
		PyErr_PrintEx(0);
		return 0;																				
	}

	if(!PyLong_Check(value))
	{
		PyErr_Format(PyExc_AssertionError, "%s: %d set shouldAutoBackup value is not int!\n",		
			scriptName(), id());		
		PyErr_PrintEx(0);
		return 0;	
	}

	shouldAutoBackup_ = (int8)PyLong_AsLong(value);
	return 0;
}

//-------------------------------------------------------------------------------------
PyObject* Entity::pyGetShouldAutoBackup()
{
	return PyLong_FromLong(shouldAutoBackup_);
}

PyObject* Entity::pyGetControllers()
{
	const ScriptTimers::Map timerMap = this->scriptTimers().map();
	PyObject* pyList = PyList_New(timerMap.size());
	int i = 0;
	uint32 time;
	uint32 interval;
	void* pUser;
	for (ScriptTimers::Map::const_iterator it = timerMap.begin(); it != timerMap.end();it++)
	{
		Baseapp::getSingleton().timers().getTimerInfo(it->second, time, interval, pUser);
		int32 userData = int32(uintptr(pUser));

		PyObject* pyTup = PyTuple_New(3);
		PyTuple_SetItem(pyTup, 0, PyUnicode_FromString("timer"));
		PyTuple_SetItem(pyTup, 1, PyLong_FromLong(it->first));
		PyTuple_SetItem(pyTup, 2, PyLong_FromLong(userData));
		PyList_SET_ITEM(pyList, i++, pyTup);
	}
	return pyList;
}
//-------------------------------------------------------------------------------------
void Entity::onCreateCellFailure(void)
{
	SCOPED_PROFILE(SCRIPTCALL_PROFILE);

	creatingCell_ = false;
	isGetingCellData_ = false;

	CALL_ENTITY_AND_COMPONENTS_METHOD(this, SCRIPT_OBJECT_CALL_ARGS0(pyTempObj, const_cast<char*>("onCreateCellFailure"), GETERR));
}

//-------------------------------------------------------------------------------------
void Entity::onRemoteMethodCall(Network::Channel* pChannel, MemoryStream& s)
{
	SCOPED_PROFILE(SCRIPTCALL_PROFILE);

	if(isDestroyed())																				
	{																										
		ERROR_MSG(fmt::format("{}::onRemoteMethodCall: {} is destroyed!\n",											
			scriptName(), id()));

		s.done();
		return;																							
	}

	ENTITY_PROPERTY_UID componentPropertyUID = 0;
	s >> componentPropertyUID;

	ENTITY_METHOD_UID utype = 0;
	s >> utype;
	
	ScriptDefModule* pScriptModule = pScriptModule_;
	PyObject* pyCallObject = this;

	PropertyDescription* pComponentPropertyDescription = NULL;
	if (componentPropertyUID > 0)
	{
		pComponentPropertyDescription = pScriptModule_->findBasePropertyDescription(componentPropertyUID);

		if (pComponentPropertyDescription && pComponentPropertyDescription->getDataType()->type() == DATA_TYPE_ENTITY_COMPONENT)
		{
			pScriptModule = static_cast<EntityComponentType*>(pComponentPropertyDescription->getDataType())->pScriptDefModule();

			pyCallObject = PyObject_GetAttrString(this, const_cast<char*>
				(pComponentPropertyDescription->getName()));
		}
		else
		{
			ERROR_MSG(fmt::format("{2}::onRemoteMethodCall: can't found EntityComponent({3}). utype={0}, methodName=unknown, callerID:{1}.\n"
				, utype, id_, this->scriptName(), (componentPropertyUID)));
		}
	}

	MethodDescription* pMethodDescription = pScriptModule->findBaseMethodDescription(utype);
	if(pMethodDescription == NULL)
	{
		ERROR_MSG(fmt::format("{2}::onRemoteMethodCall: can't found {3}method. utype={0}, methodName=unknown, callerID:{1}.\n", 
			utype, id_, this->scriptName(), 
			(pComponentPropertyDescription ? (std::string("component[") + std::string(pScriptModule->getName()) + "] ") : "")));
		
		if (pyCallObject != static_cast<PyObject*>(this))
			Py_DECREF(pyCallObject);

		s.done();
		return;
	}

	// 如果是外部通道调用则判断来源性
	if (pChannel->isExternal())
	{
		ENTITY_ID srcEntityID = pChannel->proxyID();
		if (srcEntityID <= 0 || srcEntityID != this->id())
		{
			ERROR_MSG(fmt::format("{2}::onRemoteMethodCall({3}): srcEntityID:{0} != thisEntityID:{1}! {4}\n",
				srcEntityID, this->id(), this->scriptName(), pMethodDescription->getName(), 
				(pComponentPropertyDescription ? (std::string(pScriptModule->getName()) + "::") + pMethodDescription->getName() : "")));

			if (pyCallObject != static_cast<PyObject*>(this))
				Py_DECREF(pyCallObject);

			s.done();
			pChannel->condemn("onRemoteMethodCall handler::handle: Illegal access1");
			return;
		}

		if(!pMethodDescription->isExposed())
		{
			ERROR_MSG(fmt::format("{2}::onRemoteMethodCall: {0} not is exposed, call is illegal! srcEntityID:{1}! {3}\n",
				pMethodDescription->getName(), srcEntityID, this->scriptName(), 
				(pComponentPropertyDescription ? (std::string(pScriptModule->getName()) + "::") + pMethodDescription->getName() : "")));

			if (pyCallObject != static_cast<PyObject*>(this))
				Py_DECREF(pyCallObject);

			s.done();
			return;
		}
	}

	if(g_debugEntity)
	{
		DEBUG_MSG(fmt::format("{3}::onRemoteMethodCall: {0}, {3}::{4}{1}(utype={2}).\n", 
			id_, (pMethodDescription ? pMethodDescription->getName() : "unknown"), utype, this->scriptName(),
			(pComponentPropertyDescription ? (std::string(pScriptModule->getName()) + "::") : "")));
	}

	EntityDef::context().currEntityID = this->id();

	PyObject* pyFunc = PyObject_GetAttrString(pyCallObject, const_cast<char*>(pMethodDescription->getName()));

	PyObject* pyResult = NULL;
	if(pMethodDescription != NULL)
	{
		if(pMethodDescription->getArgSize() == 0)
		{
			pyResult = pMethodDescription->call(pyFunc, NULL);
		}
		else
		{
			PyObject* pyargs = pMethodDescription->createFromStream(&s);
			if(pyargs)
			{
				pyResult = pMethodDescription->call(pyFunc, pyargs);
				Py_XDECREF(pyargs);
			}
			else
			{
				SCRIPT_ERROR_CHECK();
			}
		}
	}
	
	Py_XDECREF(pyResult);
	Py_XDECREF(pyFunc);

	if (pyCallObject != static_cast<PyObject*>(this))
		Py_DECREF(pyCallObject);
}

//-------------------------------------------------------------------------------------
void Entity::onGetCell(Network::Channel* pChannel, COMPONENT_ID componentID)
{
	if(pChannel->isExternal())
		return;
	
	SCOPED_PROFILE(SCRIPTCALL_PROFILE);

	creatingCell_ = false;

	// 删除cellData属性
	destroyCellData();
	
	// 回调给脚本，获得了cell
	if(cellEntityCall_ == NULL)
		cellEntityCall_ = new EntityCall(pScriptModule_, NULL, componentID, id_, ENTITYCALL_TYPE_CELL);

	if (!inRestore_)
	{
		CALL_ENTITY_AND_COMPONENTS_METHOD(this, SCRIPT_OBJECT_CALL_ARGS0(pyTempObj, const_cast<char*>("onGetCell"), GETERR));
	}

	if (!isDestroyed() && hasFlags(ENTITY_FLAGS_DESTROY_AFTER_GETCELL))
	{
		WARNING_MSG(fmt::format("{}::onGetCell(): Automatically destroy cell! id={}.\n",
			this->scriptName(), this->id()));

		destroyCellEntity();
	}
}

//-------------------------------------------------------------------------------------
void Entity::onClientDeath()
{
	SCOPED_PROFILE(SCRIPTCALL_PROFILE);
	CALL_COMPONENTS_AND_ENTITY_METHOD(this, SCRIPT_OBJECT_CALL_ARGS0(pyTempObj, const_cast<char*>("onClientDeath"), GETERR));
}

//-------------------------------------------------------------------------------------
//add by huyf 2019.07.08:实体销毁-8
void Entity::onLoseCell(Network::Channel* pChannel, MemoryStream& s)
{
	DEBUG_MSG(fmt::format("huyf-yh: Entity::onLoseCell scriptName={} id={}\n", this->scriptName(), this->id()));

	if(pChannel->isExternal())
		return;
	
	SCOPED_PROFILE(SCRIPTCALL_PROFILE);

	S_RELEASE(cellEntityCall_);

	isArchiveing_ = false;
	isGetingCellData_ = false;
	createdSpace_ = false;
	
	CALL_COMPONENTS_AND_ENTITY_METHOD(this, SCRIPT_OBJECT_CALL_ARGS0(pyTempObj, const_cast<char*>("onLoseCell"), GETERR));

	if (!isDestroyed() && hasFlags(ENTITY_FLAGS_DESTROY_AFTER_GETCELL))
	{
		WARNING_MSG(fmt::format("{}::onLoseCell(): Automatically destroy! id={}.\n",
			this->scriptName(), this->id()));

		destroy();
	}
}

//-------------------------------------------------------------------------------------
void Entity::onRestore()
{
	if(!inRestore_)
		return;

	SCOPED_PROFILE(SCRIPTCALL_PROFILE);
	CALL_ENTITY_AND_COMPONENTS_METHOD(this, SCRIPT_OBJECT_CALL_ARGS0(pyTempObj, const_cast<char*>("onRestore"), GETERR));

	inRestore_ = false;
	isArchiveing_ = false;
	removeFlags(ENTITY_FLAGS_INITING);
}

//-------------------------------------------------------------------------------------
void Entity::reqBackupCellData()
{
	if(isGetingCellData_)
		return;

	EntityCall* mb = this->cellEntityCall();
	if(mb == NULL)
		return;

	Network::Bundle* pBundle = Network::Bundle::createPoolObject(OBJECTPOOL_POINT);
	(*pBundle).newMessage(CellappInterface::reqBackupEntityCellData);
	(*pBundle) << this->id();
	sendToCellapp(pBundle);

	isGetingCellData_ = true;
}

//-------------------------------------------------------------------------------------
void Entity::onBackupCellData(Network::Channel* pChannel, MemoryStream& s)
{
	if(pChannel->isExternal())
		return;
	
	isGetingCellData_ = false;

	bool isDirty = false;
	s >> isDirty;
	
	if(isDirty)
	{		
		PyObject* cellData = createCellDataFromStream(&s);
		installCellDataAttr(cellData);
		Py_DECREF(cellData);
		setDirty();
	}
}

//-------------------------------------------------------------------------------------
void Entity::writeBackupData(MemoryStream* s)
{
	onBackup();
}

//-------------------------------------------------------------------------------------
void Entity::onBackup()
{
	reqBackupCellData();
}

//-------------------------------------------------------------------------------------
void Entity::writeToDB(void* data, void* extra1, void* extra2)
{
	//add by huyf 2019.06.20
	INFO_MSG(fmt::format("huyf-yh: Entity::writeToDB scriptName={} id={}.\n", this->scriptName(), this->id()));
	//add end

	PyObject* pyCallback = NULL;
	int8 shouldAutoLoad = dbid() <= 0 ? 0 : -1;

	// data 是有可能会NULL的， 比如定时存档是不需要回调函数的
	if(data != NULL)
		pyCallback = static_cast<PyObject*>(data);

	if(extra1 != NULL && (*static_cast<int*>(extra1)) != -1)
		shouldAutoLoad = (*static_cast<int*>(extra1)) > 0 ? 1 : 0;

	if (extra2)
	{
		if (strlen(static_cast<char*>(extra2)) > 0)
		{
			DBInterfaceInfo* pDBInterfaceInfo = g_kbeSrvConfig.dbInterface(static_cast<char*>(extra2));
			if (pDBInterfaceInfo->isPure)
			{
				ERROR_MSG(fmt::format("Entity::writeToDB: dbInterface({}) is a pure database does not support Entity! "
					"kbengine[_defs].xml->dbmgr->databaseInterfaces->*->pure\n",
					static_cast<char*>(extra2)));

				return;
			}

			int dbInterfaceIndex = pDBInterfaceInfo->index;
			if (dbInterfaceIndex >= 0)
			{
				dbInterfaceIndex_ = dbInterfaceIndex;
			}
			else
			{
				ERROR_MSG(fmt::format("Entity::writeToDB: not found dbInterface({})!\n",
					static_cast<char*>(extra2)));

				return;
			}
		}
	}

	if(isArchiveing_)
	{
		// __py_pyWriteToDB没有增加引用
		//if(pyCallback != NULL)
		//	Py_DECREF(pyCallback);

		WARNING_MSG(fmt::format("{}::writeToDB(): is archiveing! entityid={}, dbid={}.\n", 
			this->scriptName(), this->id(), this->dbid()));

		return;
	}

	isArchiveing_ = true;

	if(isDestroyed())
	{	
		// __py_pyWriteToDB没有增加引用
		//if(pyCallback != NULL)
		//	Py_DECREF(pyCallback);

		ERROR_MSG(fmt::format("{}::writeToDB(): is destroyed! entityid={}, dbid={}.\n", 
			this->scriptName(), this->id(), this->dbid()));

		return;
	}

	CALLBACK_ID callbackID = 0;
	if(pyCallback != NULL)
	{
		callbackID = callbackMgr().save(pyCallback);
	}

	// creatingCell_ 此时可能正在创建cell
	// 不过我们在此假设在cell未创建完成的时候base这个接口被调用
	// 写入数据库的是该entity的初始值， 并不影响
	if(this->cellEntityCall() == NULL) 
	{
		onCellWriteToDBCompleted(callbackID, shouldAutoLoad, -1);
	}
	else
	{
		Network::Bundle* pBundle = Network::Bundle::createPoolObject(OBJECTPOOL_POINT);
		(*pBundle).newMessage(CellappInterface::reqWriteToDBFromBaseapp);
		(*pBundle) << this->id();
		(*pBundle) << callbackID;
		(*pBundle) << shouldAutoLoad;
		sendToCellapp(pBundle);
	}
}

//-------------------------------------------------------------------------------------
void Entity::onWriteToDBCallback(ENTITY_ID eid, 
								DBID entityDBID, 
								uint16 dbInterfaceIndex,
								CALLBACK_ID callbackID, 
								int8 shouldAutoLoad,
								bool success)
{
	isArchiveing_ = false;

	PyObjectPtr pyCallback;

	if(callbackID > 0)
		pyCallback = callbackMgr().take(callbackID);

	if(dbid() <= 0)
	{
		dbid(dbInterfaceIndex, entityDBID);
	}
	
	if (dbid() <= 0)
	{
		KBE_ASSERT(!success);
		hasDB(false);
	}

	if(callbackID > 0)
	{
		PyObject* pyargs = PyTuple_New(2);

		Py_INCREF(this);
		PyTuple_SET_ITEM(pyargs, 0, PyBool_FromLong((long)success));
		PyTuple_SET_ITEM(pyargs, 1, this);
		
		if(pyCallback != NULL)
		{
			PyObject* pyRet = PyObject_CallObject(pyCallback.get(), pyargs);
			if(pyRet == NULL)
			{
				SCRIPT_ERROR_CHECK();
			}
			else
			{
				Py_DECREF(pyRet);
			}
		}
		else
		{
			ERROR_MSG(fmt::format("{}::onWriteToDBCallback: not found callback:{}.\n",
				this->scriptName(), callbackID));
		}

		Py_DECREF(pyargs);
	}
}

//-------------------------------------------------------------------------------------
void Entity::onCellWriteToDBCompleted(CALLBACK_ID callbackID, int8 shouldAutoLoad, int dbInterfaceIndex)
{
	//add by huyf 2019.06.20
	DEBUG_MSG(fmt::format("huyf-yh: {}::onCellWriteToDBCompleted: callback:{} shouldAutoLoad:{} dbInterfaceIndex={}.\n",
		this->scriptName(), callbackID, shouldAutoLoad, dbInterfaceIndex));
	//add end

	SCOPED_PROFILE(SCRIPTCALL_PROFILE);
	CALL_ENTITY_AND_COMPONENTS_METHOD(this, SCRIPT_OBJECT_CALL_ARGS0(pyTempObj, const_cast<char*>("onPreArchive"), GETERR));

	if (dbInterfaceIndex >= 0)
		dbInterfaceIndex_ = dbInterfaceIndex;

	hasDB(true);
	
	onWriteToDB();
	
	// 如果在数据库中已经存在该entity则允许应用层多次调用写库进行数据及时覆盖需求
	if(this->DBID_ > 0)
		isArchiveing_ = false;
	
	Components::COMPONENTS& cts = Components::getSingleton().getComponents(DBMGR_TYPE);
	Components::ComponentInfos* dbmgrinfos = NULL;

	if(cts.size() > 0)
		dbmgrinfos = &(*cts.begin());

	if(dbmgrinfos == NULL || dbmgrinfos->pChannel == NULL || dbmgrinfos->cid == 0)
	{
		ERROR_MSG(fmt::format("{}::onCellWriteToDBCompleted({}): not found dbmgr!\n", 
			this->scriptName(), this->id()));

		return;
	}
	
	MemoryStream* s = MemoryStream::createPoolObject(OBJECTPOOL_POINT);

	try
	{
		addPersistentsDataToStream(ED_FLAG_ALL, s);
	}
	catch (MemoryStreamWriteOverflow & err)
	{
		ERROR_MSG(fmt::format("{}::onCellWriteToDBCompleted({}): {}\n",
			this->scriptName(), this->id(), err.what()));

		MemoryStream::reclaimPoolObject(s);
		return;
	}

	if (s->length() == 0)
	{
		MemoryStream::reclaimPoolObject(s);
		return;
	}

	KBE_SHA1 sha;
	uint32 digest[5];

	sha.Input(s->data(), s->length());
	sha.Result(digest);

	// 检查数据是否有变化，有变化则将数据备份并且记录数据hash，没变化什么也不做
	if (memcmp((void*)&persistentDigest_[0], (void*)&digest[0], sizeof(persistentDigest_)) == 0)
	{
		MemoryStream::reclaimPoolObject(s);

		if (callbackID > 0)
		{
			PyObjectPtr pyCallback = callbackMgr().take(callbackID);
			if (pyCallback != NULL)
			{
				PyObject* pyargs = PyTuple_New(2);

				Py_INCREF(this);
				PyTuple_SET_ITEM(pyargs, 0, PyBool_FromLong(1));
				PyTuple_SET_ITEM(pyargs, 1, this);

				PyObject* pyRet = PyObject_CallObject(pyCallback.get(), pyargs);
				if (pyRet == NULL)
				{
					SCRIPT_ERROR_CHECK();
				}
				else
				{
					Py_DECREF(pyRet);
				}
				Py_DECREF(pyargs);
			}
			else
			{
				ERROR_MSG(fmt::format("{}::onWriteToDBCallback: not found callback:{}.\n",
					this->scriptName(), callbackID));
			}			
		}
		return;
	}
	else
	{
		setDirty((uint32*)&digest[0]);
	}

	Network::Bundle* pBundle = Network::Bundle::createPoolObject(OBJECTPOOL_POINT);
	(*pBundle).newMessage(DbmgrInterface::writeEntity);

	(*pBundle) << g_componentID;
	(*pBundle) << this->id();
	(*pBundle) << this->dbid();
	(*pBundle) << this->dbInterfaceIndex();
	(*pBundle) << this->pScriptModule()->getUType();
	(*pBundle) << callbackID;
	(*pBundle) << shouldAutoLoad;

	// 记录登录地址
	if(this->dbid() == 0)
	{
		uint32 ip = 0;
		uint16 port = 0;
		
		if(this->clientEntityCall())
		{
			ip = this->clientEntityCall()->addr().ip;
			port = this->clientEntityCall()->addr().port;
		}

		(*pBundle) << ip;
		(*pBundle) << port;
	}

	(*pBundle).append(*s);

	dbmgrinfos->pChannel->send(pBundle);
	MemoryStream::reclaimPoolObject(s);
}

//-------------------------------------------------------------------------------------
void Entity::onWriteToDB()
{
	//add by huyf 2019.06.20
	DEBUG_MSG(fmt::format("huyf-yh: {}::onWriteToDB.\n",
		this->scriptName()));
	//add end

	SCOPED_PROFILE(SCRIPTCALL_PROFILE);

	PyObject* cd = cellDataDict_;
	if (!cd)
		cd = Py_None;

	CALL_ENTITY_AND_COMPONENTS_METHOD(this, SCRIPT_OBJECT_CALL_ARGS1(pyTempObj, const_cast<char*>("onWriteToDB"), const_cast<char*>("O"), cd, GETERR));
}

//-------------------------------------------------------------------------------------
void Entity::onCellAppDeath()
{
	isArchiveing_ = false;
	isGetingCellData_ = false;
}

//-------------------------------------------------------------------------------------
PyObject* Entity::createCellEntity(PyObject* pyobj)
{
	DEBUG_MSG(fmt::format("huyf-yh: Entity::createCellEntity: scriptName=[{}] id=[{}] \n", scriptName(), id()));

	if(isDestroyed())
	{
		PyErr_Format(PyExc_AssertionError, "%s::createCellEntity: %d is destroyed!\n",
			scriptName(), id());
		PyErr_PrintEx(0);
		return 0;
	}

	if(Baseapp::getSingleton().findEntity(id()) == NULL)
	{
		PyErr_Format(PyExc_AssertionError, "%s::createCellEntity: %d not found!\n", 
			scriptName(), id());

		PyErr_PrintEx(0);
		return 0;
	}

	if(creatingCell_ || this->cellEntityCall())
	{
		PyErr_Format(PyExc_AssertionError, "%s::createCellEntity: %d has a cell!\n", 
			scriptName(), id());

		PyErr_PrintEx(0);
		return 0;
	}

	if(!PyObject_TypeCheck(pyobj, EntityCall::getScriptType()))
	{
		PyErr_Format(PyExc_TypeError, "create %s arg1 is not cellEntityCall!", 
			this->scriptName());

		PyErr_PrintEx(0);
		return 0;
	}
	
	EntityCallAbstract* cellEntityCall = static_cast<EntityCallAbstract*>(pyobj);
	if(cellEntityCall->type() != ENTITYCALL_TYPE_CELL)
	{
		PyErr_Format(PyExc_TypeError, "create %s args1 not is a direct cellEntityCall!", 
			this->scriptName());

		PyErr_PrintEx(0);
		return 0;
	}
	
	creatingCell_ = true;
	Baseapp::getSingleton().createCellEntity(cellEntityCall, this);
	S_Return;
}

//-------------------------------------------------------------------------------------
void Entity::restoreCell(EntityCallAbstract* cellEntityCall)
{
	if(creatingCell_ || inRestore_) return;

	creatingCell_ = true;
	inRestore_ = true;

	if(createdSpace_)
	{
		Baseapp::getSingleton().restoreSpaceInCell(this);
	}
	else
	{
		Baseapp::getSingleton().createCellEntity(cellEntityCall, this);
	}
}

//-------------------------------------------------------------------------------------
PyObject* Entity::createCellEntityInNewSpace(PyObject* args)
{
	if(isDestroyed())
	{
		PyErr_Format(PyExc_AssertionError, "%s::createCellEntityInNewSpace: %d is destroyed!\n",
			scriptName(), id());
		PyErr_PrintEx(0);
		return 0;
	}	

	if(createdSpace_ || this->cellEntityCall() != NULL)
	{
		PyErr_Format(PyExc_AssertionError, "%s::createCellEntityInNewSpace: %d in space!\n", 
			scriptName(), id());

		PyErr_PrintEx(0);
		return 0;
	}

	createdSpace_ = true;
	Baseapp::getSingleton().createCellEntityInNewSpace(this, args);
	S_Return;
}

//-------------------------------------------------------------------------------------
void Entity::forwardEntityMessageToCellappFromClient(Network::Channel* pChannel, MemoryStream& s)
{
	if(pChannel->proxyID() != this->id())
	{
		WARNING_MSG(fmt::format("{2}::forwardEntityMessageToCellappFromClient: not srcEntity({0}/{1}).\n",
			pChannel->proxyID(), this->id(), this->scriptName()));

		return;
	}

	EntityCall* mb = this->cellEntityCall();
	if(mb == NULL)
		return;

	// 将这个消息再打包转寄给cellapp， cellapp会对这个包中的每个消息进行判断
	// 检查是否是entity消息， 否则不合法.
	Network::Bundle* pBundle = Network::Bundle::createPoolObject(OBJECTPOOL_POINT);
	(*pBundle).newMessage(CellappInterface::forwardEntityMessageToCellappFromClient);
	(*pBundle) << this->id();
	(*pBundle).append(s);
	sendToCellapp(pBundle);
}

//-------------------------------------------------------------------------------------
PyObject* Entity::pyTeleport(PyObject* baseEntityMB)
{
	if(isDestroyed())
	{
		PyErr_Format(PyExc_AssertionError, "%s::teleport: %d is destroyed!\n",
			scriptName(), id());
		PyErr_PrintEx(0);
		return 0;
	}	

	if(this->cellEntityCall() == NULL)
	{
		PyErr_Format(PyExc_AssertionError, "%s::teleport: %d no has cell!\n", 
			scriptName(), id());

		PyErr_PrintEx(0);
		return 0;
	}

	if(baseEntityMB == NULL)
	{
		PyErr_Format(PyExc_Exception, "%s::teleport: %d baseEntityMB is NULL!\n", 
			scriptName(), id());

		PyErr_PrintEx(0);
		return 0;
	}

	bool isEntityCall = PyObject_TypeCheck(baseEntityMB, EntityCall::getScriptType());
	bool isEntity = !isEntityCall && (PyObject_TypeCheck(baseEntityMB, Entity::getScriptType())
		|| PyObject_TypeCheck(baseEntityMB, Proxy::getScriptType()));

	if(!isEntityCall && !isEntity)
	{
		PyErr_Format(PyExc_AssertionError, "%s::teleport: %d invalid baseEntityMB!\n", 
			scriptName(), id());

		PyErr_PrintEx(0);
		return 0;
	}

	ENTITY_ID eid = 0;

	// 如果不是entityCall则是本地base
	if(isEntityCall)
	{
		EntityCall* mb = static_cast<EntityCall*>(baseEntityMB);

		if(mb->type() != ENTITYCALL_TYPE_BASE && mb->type() != ENTITYCALL_TYPE_CELL_VIA_BASE)
		{
			PyErr_Format(PyExc_AssertionError, "%s::teleport: %d baseEntityMB is not baseEntityCall!\n", 
				scriptName(), id());

			PyErr_PrintEx(0);
			return 0;
		}

		eid = mb->id();

		Network::Bundle* pBundle = Network::Bundle::createPoolObject(OBJECTPOOL_POINT);
		(*pBundle).newMessage(BaseappInterface::reqTeleportOther);
		(*pBundle) << eid;

		BaseappInterface::reqTeleportOtherArgs3::staticAddToBundle((*pBundle), this->id(), 
			this->cellEntityCall()->componentID(), g_componentID);

		mb->sendCall(pBundle);
	}
	else
	{
		Entity* pEntity = static_cast<Entity*>(baseEntityMB);
		if(!pEntity->isDestroyed())
		{
			pEntity->reqTeleportOther(NULL, this->id(), 
				this->cellEntityCall()->componentID(), g_componentID);
		}
		else
		{
			PyErr_Format(PyExc_AssertionError, "%s::teleport: %d baseEntity is destroyed!\n", 
				scriptName(), id());

			PyErr_PrintEx(0);
			return 0;
		}
	}

	S_Return;
}

//-------------------------------------------------------------------------------------
void Entity::onTeleportCB(Network::Channel* pChannel, SPACE_ID spaceID, bool fromCellTeleport)
{
	if(pChannel->isExternal())
		return;
	
	if(spaceID > 0)
	{
		if(!fromCellTeleport)
			onTeleportSuccess(spaceID);
		else
			this->spaceID(spaceID);
	}
	else
	{
		onTeleportFailure();
	}
}

//-------------------------------------------------------------------------------------
void Entity::onTeleportFailure()
{
	SCOPED_PROFILE(SCRIPTCALL_PROFILE);
	CALL_ENTITY_AND_COMPONENTS_METHOD(this, SCRIPT_OBJECT_CALL_ARGS0(pyTempObj, const_cast<char*>("onTeleportFailure"), GETERR));
}

//-------------------------------------------------------------------------------------
void Entity::onTeleportSuccess(SPACE_ID spaceID)
{
	SCOPED_PROFILE(SCRIPTCALL_PROFILE);

	this->spaceID(spaceID);
	CALL_ENTITY_AND_COMPONENTS_METHOD(this, SCRIPT_OBJECT_CALL_ARGS0(pyTempObj, const_cast<char*>("onTeleportSuccess"), GETERR));
}

//-------------------------------------------------------------------------------------
void Entity::reqTeleportOther(Network::Channel* pChannel, ENTITY_ID reqTeleportEntityID, 
							COMPONENT_ID reqTeleportEntityCellAppID, COMPONENT_ID reqTeleportEntityBaseAppID)
{
	if (pChannel && pChannel->isExternal())
		return;
	
	DEBUG_MSG(fmt::format("{2}::reqTeleportOther: reqTeleportEntityID={0}, reqTeleportEntityCellAppID={1}.\n",
		reqTeleportEntityID, reqTeleportEntityCellAppID, this->scriptName()));

	if(this->cellEntityCall() == NULL || this->cellEntityCall()->getChannel() == NULL)
	{
		ERROR_MSG(fmt::format("{}::reqTeleportOther: {}, teleport error, cellEntityCall is NULL, "
			"reqTeleportEntityID={}, reqTeleportEntityCellAppID={}.\n",
			this->scriptName(), this->id(), reqTeleportEntityID, reqTeleportEntityCellAppID));

		return;
	}

	Components::ComponentInfos* cinfos = Components::getSingleton().findComponent(reqTeleportEntityCellAppID);
	if(cinfos == NULL || cinfos->pChannel == NULL)
	{
		ERROR_MSG(fmt::format("{}::reqTeleportOther: {}, teleport error, not found cellapp, "
			"reqTeleportEntityID={}, reqTeleportEntityCellAppID={}.\n",
			this->scriptName(), this->id(), reqTeleportEntityID, reqTeleportEntityCellAppID));

		return;
	}

	if (pBufferedSendToClientMessages_ || hasFlags(ENTITY_FLAGS_TELEPORT_START) || hasFlags(ENTITY_FLAGS_TELEPORT_STOP))
	{
		ERROR_MSG(fmt::format("{}::reqTeleportOther: {}, teleport error, in transit, "
			"reqTeleportEntityID={}, reqTeleportEntityCellAppID={}.\n",
			this->scriptName(), this->id(), reqTeleportEntityID, reqTeleportEntityCellAppID));

		return;
	}

	Network::Bundle* pBundle = Network::Bundle::createPoolObject(OBJECTPOOL_POINT);
	(*pBundle).newMessage(CellappInterface::teleportFromBaseapp);
	(*pBundle) << reqTeleportEntityID;

	CellappInterface::teleportFromBaseappArgs3::staticAddToBundle((*pBundle), this->cellEntityCall()->componentID(), 
		this->id(), reqTeleportEntityBaseAppID);
	
	sendToCellapp(cinfos->pChannel, pBundle);
}

//-------------------------------------------------------------------------------------
void Entity::onMigrationCellappStart(Network::Channel* pChannel, COMPONENT_ID sourceCellAppID, COMPONENT_ID targetCellAppID)
{
	if (pChannel && pChannel->isExternal())
		return;
	
	DEBUG_MSG(fmt::format("{}::onMigrationCellappStart: {}, sourceCellAppID={}, targetCellappID={}\n",
		scriptName(), id(), sourceCellAppID, targetCellAppID));

	if (hasFlags(ENTITY_FLAGS_TELEPORT_STOP))
	{
		removeFlags(ENTITY_FLAGS_TELEPORT_STOP);

		KBE_ASSERT(pBufferedSendToClientMessages_);
		pBufferedSendToClientMessages_->startForward();
	}
	else
	{
		addFlags(ENTITY_FLAGS_TELEPORT_START);
	}
}

//-------------------------------------------------------------------------------------
void Entity::onMigrationCellappEnd(Network::Channel* pChannel, COMPONENT_ID sourceCellAppID, COMPONENT_ID targetCellAppID)
{
	if (pChannel && pChannel->isExternal())
		return;
	
	DEBUG_MSG(fmt::format("{}::onMigrationCellappEnd: {}, sourceCellAppID={}, targetCellappID={}\n",
		scriptName(), id(), sourceCellAppID, targetCellAppID));

	KBE_ASSERT(!pBufferedSendToClientMessages_);
	
	// 某些极端情况下可能onMigrationCellappStart会慢于onMigrationCellappEnd触发，此时必须设置标记
	// 等待onMigrationCellappEnd触发后做清理
	if (!hasFlags(ENTITY_FLAGS_TELEPORT_START))
	{
		addFlags(ENTITY_FLAGS_TELEPORT_STOP);

		if (pBufferedSendToClientMessages_ == NULL)
			pBufferedSendToClientMessages_ = new BaseMessagesForwardClientHandler(this, targetCellAppID);

		pBufferedSendToClientMessages_->stopForward();
	}
	else
	{
		removeFlags(ENTITY_FLAGS_TELEPORT_START);
		onMigrationCellappOver(targetCellAppID);
	}
}

//-------------------------------------------------------------------------------------
void Entity::onMigrationCellappOver(COMPONENT_ID targetCellAppID)
{
	Components::ComponentInfos* pInfos = Components::getSingleton().findComponent(targetCellAppID);
	if (pInfos && pInfos->pChannel)
	{
		Network::Bundle* pBundle = Network::Bundle::createPoolObject(OBJECTPOOL_POINT);
		(*pBundle).newMessage(CellappInterface::reqTeleportToCellAppOver);
		(*pBundle) << id();
		pInfos->pChannel->send(pBundle);
	}
	
	// 改变cell的指向到新的cellapp
	if(this->cellEntityCall())
		this->cellEntityCall()->componentID(targetCellAppID);
}

//-------------------------------------------------------------------------------------
void Entity::onBufferedForwardToCellappMessagesOver()
{
}

//-------------------------------------------------------------------------------------
void Entity::onBufferedForwardToClientMessagesOver()
{
	onMigrationCellappOver(pBufferedSendToClientMessages_->cellappID());
	SAFE_RELEASE(pBufferedSendToClientMessages_);
}

//-------------------------------------------------------------------------------------
void Entity::onGetDBID(Network::Channel* pChannel, DBID dbid)
{
	if(pChannel->isExternal())
		return;
}

//-------------------------------------------------------------------------------------
void Entity::onTimer(ScriptID timerID, int useraAgs)
{
	SCOPED_PROFILE(ONTIMER_PROFILE);
	
	CALL_ENTITY_AND_COMPONENTS_METHOD(this, SCRIPT_OBJECT_CALL_ARGS2(pyTempObj, const_cast<char*>("onTimer"),
		const_cast<char*>("Ii"), timerID, useraAgs, GETERR));
}

//-------------------------------------------------------------------------------------
bool Entity::_reload(bool fullReload)
{
	return true;
}

void Entity::initProperty(bool isReload)
{
	DEBUG_MSG(fmt::format("Entity huyf-yh: Entity::initProperty isReload=[{}].\n", isReload));
	EntityDef::context().currComponentType = g_componentType;
	EntityDef::context().currEntityID = id();

	ScriptDefModule::PROPERTYDESCRIPTION_MAP* oldpropers = NULL;
	if (isReload)
	{
		ScriptDefModule* pOldScriptDefModule = EntityDef::findOldScriptModule(pScriptModule_->getName());
		if (!pOldScriptDefModule)
		{
			ERROR_MSG(fmt::format("{}::initProperty: not found old_module!\n", pScriptModule_->getName()));
			KBE_ASSERT(false && "Entity::initProperty: not found old_module");
		}

		oldpropers = &pOldScriptDefModule->getPropertyDescrs();
	}

	ScriptDefModule::PROPERTYDESCRIPTION_MAP::const_iterator iter = pPropertyDescrs_->begin();
	for (; iter != pPropertyDescrs_->end(); ++iter)
	{
		PropertyDescription* propertyDescription = iter->second;
		DataType* dataType = propertyDescription->getDataType();
		if (oldpropers)
		{
			ScriptDefModule::PROPERTYDESCRIPTION_MAP::iterator olditer = oldpropers->find(iter->first);
			if (olditer != oldpropers->end())
			{
				if (strcmp(olditer->second->getDataType()->getName(), propertyDescription->getDataType()->getName()) == 0 &&
					strcmp(olditer->second->getDataType()->getName(), propertyDescription->getDataType()->getName()) == 0)
					continue;
			}
		}

		if (dataType)
		{
			DEBUG_MSG(fmt::format("Entity huyf-yh: Entity::initProperty-newDefaultVal isReload=[{}].\n", isReload));
			PyObject* defObj = propertyDescription->newDefaultVal();

			if (dataType->type() == DATA_TYPE_ENTITY_COMPONENT)
				((EntityComponent*)defObj)->updateOwner(id(), this);

			PyObject_SetAttrString(static_cast<PyObject*>(this), propertyDescription->getName(), defObj);
			Py_DECREF(defObj);

			/*DEBUG_MSG(fmt::format(#CLASS" huyf-yh: "#CLASS":: added [{}]->[{}] property ref={}.\n",	*/
			/*propertyDescription->getName(), defObj->ob_type->tp_name, defObj->ob_refcnt));			*/
		}
		else
		{
			ERROR_MSG(fmt::format("{}::initProperty: {} dataType is NULL！ entityID={}\n", scriptName(), propertyDescription->getName(), id()));
		}
	}

	/* 由于组件初始化时会自动initProperty，所以只有重加载时需要显示的去调用组件的initProperty */
	if (isReload)
	{
		const ScriptDefModule::COMPONENTDESCRIPTION_MAP* pComponentDescrs = &pScriptModule_->getComponentDescrs();

		ScriptDefModule::COMPONENTDESCRIPTION_MAP::const_iterator iter1 = pComponentDescrs->begin();
		for (; iter1 != pComponentDescrs->end(); ++iter1)
		{
			PyObject* pComponentProperty = PyObject_GetAttrString(this, iter1->first.c_str());
			if (pComponentProperty)
			{
				if (PyObject_TypeCheck(pComponentProperty, EntityComponent::getScriptType()))
				{
					EntityComponent* pEntityComponent = static_cast<EntityComponent*>(pComponentProperty);
					pEntityComponent->initProperty();
				}
				else
				{
					PyErr_Format(PyExc_AssertionError, "%s.%s is not property of EntityComponent!", scriptName(), iter1->first.c_str());
					PyErr_PrintEx(0);
				}

				Py_DECREF(pComponentProperty);
			}
			else
			{
				PyErr_Clear();
			}
		}
	}
}

int Entity::onScriptDelAttribute(PyObject* attr)
{
	char* ccattr = PyUnicode_AsUTF8AndSize(attr, NULL);
	DEBUG_OP_ATTRIBUTE("del", attr)
	DEBUG_MSG(fmt::format("entity::onScriptDelAttribute scriptName={} name={} tp_name={}\n", scriptName(), ccattr, attr->ob_type->tp_name));

	if (pPropertyDescrs_)
	{
		ScriptDefModule::PROPERTYDESCRIPTION_MAP::const_iterator iter = pPropertyDescrs_->find(ccattr);
		if (iter != pPropertyDescrs_->end())
		{
			char err[255];
			kbe_snprintf(err, 255, "property[%s] defined in %s.def, del failed!", ccattr, scriptName());
			PyErr_SetString(PyExc_TypeError, err);
			PyErr_PrintEx(0);
			return 0;
		}
	}

	if (pScriptModule_->findMethodDescription(ccattr, g_componentType) != NULL)
	{
		char err[255];
		kbe_snprintf(err, 255, "method[%s] defined in %s.def, del failed!", ccattr, scriptName());
		PyErr_SetString(PyExc_TypeError, err);
		PyErr_PrintEx(0);
		return 0;
	}

	return ScriptObject::onScriptDelAttribute(attr);
}

int Entity::onScriptSetAttribute(PyObject* attr, PyObject* value)
{
	DEBUG_OP_ATTRIBUTE("set", attr)
	char* ccattr = PyUnicode_AsUTF8AndSize(attr, NULL);

	if (pPropertyDescrs_)
	{
		ScriptDefModule::PROPERTYDESCRIPTION_MAP::const_iterator iter = pPropertyDescrs_->find(ccattr);
		if (iter != pPropertyDescrs_->end())
		{
			PropertyDescription* propertyDescription = iter->second;
			DataType* dataType = propertyDescription->getDataType();

			if (!hasFlags(ENTITY_FLAGS_DESTROYING) && isDestroyed_)
			{
				PyErr_Format(PyExc_AssertionError, "can't set %s.%s to %s. entity is destroyed!", scriptName(), ccattr, value->ob_type->tp_name);
				PyErr_PrintEx(0);
				return 0;
			}

			if (!dataType->isSameType(value))
			{
				PyErr_Format(PyExc_ValueError, "can't set %s.%s to %s.", scriptName(), ccattr, value->ob_type->tp_name);
				PyErr_PrintEx(0);
				return 0;
			}
			else
			{
				//add by huyf 2019.09.06
				//DEBUG_MSG(fmt::format("entity::onScriptSetAttribute scriptName={} name={} tp_name={}\n", scriptName(), propertyDescription->getName(), value->ob_type->tp_name));
				//add end

				Py_ssize_t ob_refcnt = value->ob_refcnt;
				PyObject* pySetObj = propertyDescription->onSetValue(this, value);				

				/* 如果def属性数据有改变， 那么可能需要广播 */
				if (pySetObj != NULL)
				{
					onDefDataChanged(NULL, propertyDescription, pySetObj);
					if (pySetObj == value && pySetObj->ob_refcnt - ob_refcnt > 1)
						Py_DECREF(pySetObj);
				}

				return pySetObj == NULL ? -1 : 0;
			}
		}
	}

	return ScriptObject::onScriptSetAttribute(attr, value);
}

void Entity::createNamespace(PyObject* dictData, bool persistentData/* = false*/)
{
	DEBUG_MSG(fmt::format("Entity huyf-yh: Entity::createNamespace persistentData=[{}].\n", persistentData));
	if (dictData == NULL)
		return;

	if (!PyDict_Check(dictData))
	{
		ERROR_MSG(fmt::format("Entity::createNamespace: create[{}:{}] "	"args is not a dict.\n", scriptName(), id_));
		return;
	}

	EntityDef::context().currComponentType = g_componentType;
	EntityDef::context().currEntityID = id();

	Py_ssize_t pos = 0;
	PyObject *key, *value;
	PyObject* cellDataDict = PyObject_GetAttrString(this, "cellData");

	if (cellDataDict == NULL)
	{
		PyErr_Clear();
		EntityComponent::convertDictDataToEntityComponent(id(), this, pScriptModule_, dictData, persistentData);
	}

	while (PyDict_Next(dictData, &pos, &key, &value))
	{
		char* ccattrTp = PyUnicode_AsUTF8AndSize(key, NULL);

		DEBUG_MSG(fmt::format("huyf-yh: Entity::createNamespace ###### PyDict_Next pos=[{}] key=[{}] tp_typename=[{}].\n", pos, ccattrTp, value->ob_type->tp_name));

		DEBUG_CREATE_ENTITY_NAMESPACE
		if (PyObject_HasAttr(this, key) > 0)
		{
			char* ccattr = PyUnicode_AsUTF8AndSize(key, NULL);

			PropertyDescription* pCompPropertyDescription = pScriptModule_->findComponentPropertyDescription(ccattr);
			if (pCompPropertyDescription)
			{
				if (PyDict_Check(value) /* createDictDataFromPersistentStream 流程导致非字典 */)
				{
					EntityComponent* pEntityComponent = (EntityComponent*)PyObject_GetAttr(this, key);
					pEntityComponent->updateFromDict(this, value);
					Py_DECREF(pEntityComponent);
				}
				else
				{
					DEBUG_MSG(fmt::format("huyf-yh: Entity::createNamespace pCompPropertyDescription PyObject_SetAttr pos=[{}] key=[{}].\n", pos, ccattr));
					PyObject_SetAttr(this, key, value);
				}
			}
			else
			{
				DEBUG_MSG(fmt::format("huyf-yh: Entity::createNamespace ------ this->PyObject_SetAttr pos=[{}] key=[{}] tp_typename=[{}].\n", pos, ccattr, value->ob_type->tp_name));
				PyObject_SetAttr(this, key, value);
			}

			continue;
		}

		if (cellDataDict != NULL && PyDict_Contains(cellDataDict, key) > 0)
		{
			PyObject* pyVal = PyDict_GetItem(cellDataDict, key);
			if (PyDict_Check(pyVal))
			{
				if (0 != PyDict_Update(pyVal, value) /* EntityComponent required */)
				{
					SCRIPT_ERROR_CHECK();
					KBE_ASSERT(false);
				}
			}
			else
			{
				DEBUG_MSG(fmt::format(" huyf-yh: Entity::createNamespace ++++++ cellDataDict->PyDict_SetItem pos=[{}] key=[{}] tp_typename=[{}].\n", pos, ccattrTp, value->ob_type->tp_name));
				PyDict_SetItem(cellDataDict, key, value);
			}
		}
		else
		{
			char* ccattr = PyUnicode_AsUTF8AndSize(key, NULL);

			PropertyDescription* pCompPropertyDescription = pScriptModule_->findComponentPropertyDescription(ccattr);
			if (pCompPropertyDescription)
			{
				/* 一般在base上可能放在cellData中是字典，而没有cell的实体需要pass这个设置 */
				if (PyDict_Check(value))
					continue;
			}

			DEBUG_MSG(fmt::format(" huyf-yh: Entity::createNamespace ****** PyObject_SetAttr-2 pos=[{}] key=[{}] tp_typename=[{}].\n", pos, ccattrTp, value->ob_type->tp_name));
			PyObject_SetAttr(this, key, value);
		}
	}

	SCRIPT_ERROR_CHECK();

	Py_XDECREF(cellDataDict);
}

void Entity::initializeScript()
{
	DEBUG_MSG(fmt::format("Entity huyf-yh: initializeScript .\n"));
	removeFlags(ENTITY_FLAGS_INITING);
	SCOPED_PROFILE(SCRIPTCALL_PROFILE);

	const ScriptDefModule::COMPONENTDESCRIPTION_MAP* pComponentDescrs = &pScriptModule_->getComponentDescrs();

	ScriptDefModule::COMPONENTDESCRIPTION_MAP::const_iterator iter1 = pComponentDescrs->begin();
	for (; iter1 != pComponentDescrs->end(); ++iter1)
	{
		PyObject* pComponentProperty = PyObject_GetAttrString(this, iter1->first.c_str());
		if (pComponentProperty)
		{
			if (PyObject_TypeCheck(pComponentProperty, EntityComponent::getScriptType()))
			{
				EntityComponent* pEntityComponent = static_cast<EntityComponent*>(pComponentProperty);
				pEntityComponent->initializeScript();
			}
			else
			{
				PyErr_Format(PyExc_AssertionError, "%s.%s is not property of EntityComponent!", scriptName(), iter1->first.c_str());
				PyErr_PrintEx(0);
			}

			Py_DECREF(pComponentProperty);
		}
		else
		{
			PyErr_Clear();
		}
	}

	if (PyObject_HasAttrString(this, "__init__"))
	{
		DEBUG_MSG(fmt::format("Entity huyf-yh: {}.__init__ .\n", scriptName()));
		PyObject* pyResult = PyObject_CallMethod(this, const_cast<char*>("__init__"), const_cast<char*>(""));
		if (pyResult != NULL)
			Py_DECREF(pyResult);
		else
			SCRIPT_ERROR_CHECK();
	}

	iter1 = pComponentDescrs->begin();
	for (; iter1 != pComponentDescrs->end(); ++iter1)
	{
		PyObject* pComponentProperty = PyObject_GetAttrString(this, iter1->first.c_str());
		if (pComponentProperty)
		{
			if (PyObject_TypeCheck(pComponentProperty, EntityComponent::getScriptType()))
			{
				/*DEBUG_MSG(fmt::format(#CLASS" huyf-yh: updateOwner-onAttached .\n"));					*/
				EntityComponent* pEntityComponent = static_cast<EntityComponent*>(pComponentProperty);
				pEntityComponent->updateOwner(id(), this);
				pEntityComponent->onAttached();
			}
			else
			{
				PyErr_Format(PyExc_AssertionError, "%s.%s is not property of EntityComponent!", scriptName(), iter1->first.c_str());
				PyErr_PrintEx(0);
			}

			Py_DECREF(pComponentProperty);
		}
		else
		{
			PyErr_Clear();
		}
	}

	onInitializeScript();
}

//-------------------------------------------------------------------------------------
}
