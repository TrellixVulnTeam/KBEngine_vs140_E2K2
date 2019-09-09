// Copyright 2008-2018 Yolo Technologies, Inc. All Rights Reserved. https://www.comblockengine.com

#include "cellapp.h"
#include "entity.h"
#include "moveto_point_handler.h"	
#include "move_controller.h"	

namespace KBEngine{	


//-------------------------------------------------------------------------------------
MoveToPointHandler::MoveToPointHandler(KBEShared_ptr<Controller>& pController, int layer, const Position3D& destPos, 
											 float velocity, float distance, bool faceMovement, 
											bool moveVertically, PyObject* userarg):
destPos_(destPos),
velocity_(velocity),
faceMovement_(faceMovement),
moveVertically_(moveVertically),
pyuserarg_(userarg),
distance_(distance),
pController_(pController),
layer_(layer),
isDestroyed_(false)
{
	updatableName = "MoveToPointHandler";

	Py_INCREF(userarg);

	//std::static_pointer_cast<MoveController>(pController)->pMoveToPointHandler(this);
	static_cast<MoveController*>(pController.get())->pMoveToPointHandler(this);
	Cellapp::getSingleton().addUpdatable(this);
}

//-------------------------------------------------------------------------------------
MoveToPointHandler::MoveToPointHandler():
destPos_(),
velocity_(0.f),
faceMovement_(false),
moveVertically_(false),
pyuserarg_(NULL),
distance_(0.f),
layer_(0),
isDestroyed_(false)
{
	updatableName = "MoveToPointHandler";

	Cellapp::getSingleton().addUpdatable(this);
}

//-------------------------------------------------------------------------------------
MoveToPointHandler::~MoveToPointHandler()
{
	if(pyuserarg_ != NULL)
	{
		Py_DECREF(pyuserarg_);
	}

	//DEBUG_MSG(fmt::format("MoveToPointHandler::~MoveToPointHandler(): {}\n", (void*)this));
}

//-------------------------------------------------------------------------------------
void MoveToPointHandler::addToStream(KBEngine::MemoryStream& s)
{
	// uint8 utype = type();

	s << /*utype <<*/ destPos_.x << destPos_.y << destPos_.z << velocity_ << faceMovement_ << moveVertically_ <<
		distance_ << layer_;

	s.appendBlob(script::Pickler::pickle(pyuserarg_));
}

//-------------------------------------------------------------------------------------
void MoveToPointHandler::createFromStream(KBEngine::MemoryStream& s)
{
	s >> /*utype <<*/ destPos_.x >> destPos_.y >> destPos_.z >> velocity_ >> faceMovement_ >> moveVertically_ >>
		distance_ >> layer_;

	std::string val = "";
	s.readBlob(val);

	pyuserarg_ = script::Pickler::unpickle(val);
}

//-------------------------------------------------------------------------------------
bool MoveToPointHandler::requestMoveOver(const Position3D& oldPos)
{
	if(pController_)
	{
		if(pController_->pEntity())
			pController_->pEntity()->onMoveOver(pController_->id(), layer_, oldPos, pyuserarg_);

		// �����onMoveOver�е���cancelController��id���ᵼ��MoveController��������pController_ΪNULL
		pController_->destroy();
	}

	return true;
}

bool MoveToPointHandler::requestMoveFailure()
{
	if (pController_)
	{
		if (pController_->pEntity())
			pController_->pEntity()->onMoveFailure(pController_->id(), pyuserarg_);

		pController_->destroy();
	}

	return true;
}

//-------------------------------------------------------------------------------------
bool MoveToPointHandler::update()
{
	if (isDestroyed_)
	{
		delete this;
		return false;
	}
	
	Entity* pEntity = pController_->pEntity();
	Py_INCREF(pEntity);
	if ((((PyObject *)pEntity)->ob_type->tp_name, "CNpc") == 0)
	{
		DEBUG_MSG(fmt::format("huyf-yh: MoveToPointHandler::update-Py_INCREF p={:p} scriptName={} id={} ob_refcnt = {}\n", (void*)pEntity, pEntity->scriptName(), pEntity->id(), ((PyObject *)pEntity)->ob_refcnt));
	}

	const Position3D& dstPos = destPos();
	Position3D currpos = pEntity->position();
	Position3D currpos_backup = currpos;
	Direction3D direction = pEntity->direction();

	Vector3 movement = dstPos - currpos;
	//ȥ��Y��Ӱ��
	movement.y = 0.f;
	//if (!moveVertically_) movement.y = 0.f;
	
	bool ret = true;
	float dist_len = KBEVec3Length(&movement);

	if (dist_len < velocity_ + distance_)
	{
		//printf("dist_lendist_len < velocity_ :%f\n", dist_len);
		float y = currpos.y;

		if (distance_ > 0.0f)
		{
			// ��λ������
			KBEVec3Normalize(&movement, &movement); 
				
			if(dist_len > distance_)
			{
				movement *= distance_;
				currpos = dstPos - movement;
			}
		}
		else
		{
			currpos = dstPos;
		}

		if (!moveVertically_)
			currpos.y = y;

		ret = false;
	}
	else
	{
		// ��λ������
		KBEVec3Normalize(&movement, &movement); 

		// �ƶ�λ��
		movement *= velocity_;
		currpos += movement;
	}
	
	// �Ƿ���Ҫ�ı�����
	if (faceMovement_)
	{
		if (movement.x != 0.f || movement.z != 0.f)
			direction.yaw(movement.yaw());

		if (movement.y != 0.f)
			direction.pitch(movement.pitch());
	}
	
	// ����entity����λ�ú�����
	bool didMove = false;
	bool failed = false;
	if (!isDestroyed_)
	{	
		int dx = static_cast<int>(floor(currpos.x)) - static_cast<int>(floor(pEntity->position().x));
		int dz = static_cast<int>(floor(currpos.z)) - static_cast<int>(floor(pEntity->position().z));
		
		if (pEntity->collidable() && (dx||dz) && pEntity->hasNeighborEntity(dx, dz))
		{
			requestMoveFailure();
			failed = true;
		}
		else
		{
			didMove = true;
			//Vector3 movement = currpos - currpos_backup;
			//float dist_len = KBEVec3Length(&movement);
			//printf("dist_len---------------------------%f\n", dist_len);
			//printf("dist_len---------------------------currpos: %f,  %f, %f\n", currpos.x, currpos.y, currpos.z);
			//printf("dist_len---------------------------currpos_backup: %f,  %f, %f\n", currpos_backup.x, currpos_backup.y, currpos_backup.z);
			pEntity->setPositionAndDirection(currpos, direction);
		}
	}

	// ��navigate������ȷ�����ڵ�����
	if(!isDestroyed_)
		pEntity->isOnGround(isOnGround());

	// ֪ͨ�ű�
	if(!isDestroyed_ && didMove)
		pEntity->onMove(pController_->id(), layer_, currpos_backup, pyuserarg_);

	// �����onMove�����б�ֹͣ���ֻ��ߴﵽĿ�ĵ��ˣ���ֱ�����ٲ�����false
	if (isDestroyed_ || 
		(!ret && requestMoveOver(currpos_backup)) ||
		failed
		)
	{
		Py_DECREF(pEntity);
		if ((((PyObject *)pEntity)->ob_type->tp_name, "CNpc") == 0)
		{
			DEBUG_MSG(fmt::format("huyf-yh: MoveToPointHandler::update-Py_DECREF p={:p} scriptName={} id={} ob_refcnt = {}\n", (void*)pEntity, pEntity->scriptName(), pEntity->id(), ((PyObject *)pEntity)->ob_refcnt));
		}
		delete this;
		return false;
	}

	Py_DECREF(pEntity);
	if ((((PyObject *)pEntity)->ob_type->tp_name, "CNpc") == 0)
	{
		DEBUG_MSG(fmt::format("huyf-yh: MoveToPointHandler::update-Py_DECREF p={:p} scriptName={} id={} ob_refcnt = {}\n", (void*)pEntity, pEntity->scriptName(), pEntity->id(), ((PyObject *)pEntity)->ob_refcnt));
	}
	return true;
}

//-------------------------------------------------------------------------------------
}

