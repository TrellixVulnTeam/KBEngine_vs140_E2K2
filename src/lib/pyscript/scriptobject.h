// Copyright 2008-2018 Yolo Technologies, Inc. All Rights Reserved. https://www.comblockengine.com

#ifndef KBE_SCRIPTOBJECT_H
#define KBE_SCRIPTOBJECT_H

#include <vector>	
#include "Python.h"     
#include "py_macros.h" 
#include "helper/debug_helper.h"
#include "common/common.h"
#include <structmember.h>
#include <assert.h>

namespace KBEngine {
	namespace script {

		// python的默认空返回值
#define S_Return { Py_INCREF(Py_None); return Py_None; }			
#define S_RETURN S_Return	
#define PY_RETURN S_Return	

#define S_INCREF(pyObj)																		\
	if(pyObj){																				\
		Py_INCREF(pyObj);																	\
	}																						\

#define S_DECREF(pyObj)																		\
	if(pyObj){																				\
		Py_DECREF(pyObj);																	\
	}																						\

// python的对象释放
#define S_RELEASE(pyObj)																	\
	if(pyObj){																				\
		Py_DECREF(pyObj);																	\
		pyObj = NULL;																		\
	}																						\

/// 输出当前脚本产生的错误信息	
#define SCRIPT_ERROR_CHECK()																\
{																							\
 	if (PyErr_Occurred())																	\
 	{																						\
		PyErr_PrintEx(0);																	\
	}																						\
}

// 脚本对象头 （通常是python默认分配对象方式产生的对象 ）pyscript\scriptobject
#define SCRIPT_OBJECT_HREADER(CLASS, SUPERCLASS)											\
	SCRIPT_HREADER_BASE(CLASS, SUPERCLASS);													\
	/** python创建的对象则对象从python中释放
	*/																						\
		static void _tp_dealloc(PyObject* self)													\
	{																						\
		(strcmp(((PyObject *)(self))->ob_type->tp_name, "CNpc") == 0)?printf("1 [%s(%d)] _tp_dealloc %s op=%p ob_refcnt=%d\n", TrimFilePath(__FILE__), __LINE__, ((PyObject *)(self))->ob_type->tp_name, (void*)(self), ((PyObject *)(self))->ob_refcnt) : 0; \
		CLASS::_scriptType.tp_free(self);													\
	}																						\
		/*add by huyf 2019.07.10*/																\
		static int _tp_print(PyObject *op, FILE *f, int n)										\
	{																						\
		/*DEBUG_MSG(fmt::format("huyf-yh: _tp_print(op) p={:p} scriptName={} ob_refcnt={}\n", (void*)op, ((PyObject *)(op))->ob_type->tp_name, ((PyObject *)(op))->ob_refcnt)); */\
		return 0;																			\
	}																						\

		// 基础脚本对象头 （这个模块通常是提供给python脚本中进行继承的一个基础类 ）baseapp\entity baseapp\proxy baseapp\space cellapp\entity cellapp\space
#define BASE_SCRIPT_HREADER(CLASS, SUPERCLASS)												\
	SCRIPT_HREADER_BASE(CLASS, SUPERCLASS);													\
	/** python创建的对象则对象从python中释放
	*/																						\
		static void _tp_dealloc(PyObject* self)													\
	{																						\
		(strcmp(((PyObject *)(self))->ob_type->tp_name, "CNpc") == 0) ? printf("2 [%s(%d)] _tp_dealloc %s op=%p ob_refcnt=%d\n", TrimFilePath(__FILE__), __LINE__, ((PyObject *)(self))->ob_type->tp_name, (void*)(self), ((PyObject *)(self))->ob_refcnt) : 0; \
		static_cast<CLASS*>(self)->~CLASS();												\
		CLASS::_scriptType.tp_free(self);													\
	}																						\
		/*add by huyf 2019.07.10*/																\
		static int _tp_print(PyObject *op, FILE *f, int n)										\
	{																						\
		/*DEBUG_MSG(fmt::format("huyf-yh: _tp_print(op) p={:p} scriptName={} ob_refcnt={}\n", (void*)op, ((PyObject *)(op))->ob_type->tp_name, ((PyObject *)(op))->ob_refcnt)); */\
		return 0;																			\
	}																						\

		// 实例脚本对象头 （这个脚本对象是由c++中进行new产生的 ）
#define INSTANCE_SCRIPT_HREADER(CLASS, SUPERCLASS)											\
	SCRIPT_HREADER_BASE(CLASS, SUPERCLASS);													\
	/** c++new创建的对象则进行delete操作
	*/																						\
		static void _tp_dealloc(PyObject* self)													\
	{																						\
		(strcmp(((PyObject *)(self))->ob_type->tp_name, "CNpc") == 0) ? printf("3 [%s(%d)] _tp_dealloc %s op=%p ob_refcnt=%d\n", TrimFilePath(__FILE__), __LINE__, ((PyObject *)(self))->ob_type->tp_name, (void*)(self), ((PyObject *)(self))->ob_refcnt) : 0; \
		delete static_cast<CLASS*>(self);													\
	}																						\
	/*add by huyf 2019.07.10*/																\
	static int _tp_print(PyObject *op, FILE *f, int n)										\
	{																						\
		/*DEBUG_MSG(fmt::format("huyf-yh: _tp_print(op) p={:p} scriptName={} ob_refcnt={}\n", (void*)op, ((PyObject *)(op))->ob_type->tp_name, ((PyObject *)(op))->ob_refcnt)); */\
		return 0;																			\
	}																						\

																							
#define SCRIPT_HREADER_BASE(CLASS, SUPERCLASS)												\
	/* 当前脚本模块的类别 */																	\
	static PyTypeObject _scriptType;														\
	typedef CLASS ThisClass;																\
																							\
	static PyObject* _tp_repr(PyObject* self)												\
	{																						\
		return static_cast<CLASS*>(self)->tp_repr();										\
	}																						\
																							\
	static PyObject* _tp_str(PyObject* self)												\
	{																						\
		return static_cast<CLASS*>(self)->tp_str();											\
	}																						\
																							\
	/** 脚本模块对象从python中创建
	*/																						\
	static PyObject* _tp_new(PyTypeObject* type, PyObject* args, PyObject* kwds)			\
	{																						\
		return CLASS::tp_new(type, args, kwds);												\
	}																						\
																							\
	/** python 请求获取本模块的属性或者方法
	*/																						\
	static PyObject* _tp_getattro(PyObject* self, PyObject* name)							\
	{																						\
		return static_cast<CLASS*>(self)->onScriptGetAttribute(name);						\
	}																						\
																							\
	/** python 请求设置本模块的属性或者方法
	*/																						\
	static int _tp_setattro(PyObject* self, PyObject* name, PyObject* value)				\
	{																						\
		return (value != NULL) ?															\
				static_cast<CLASS*>(self)->onScriptSetAttribute(name, value):				\
				static_cast<CLASS*>(self)->onScriptDelAttribute(name);						\
	}																						\
																							\
	/** python 请求初始化本模块对象
	*/																						\
	static int _tp_init(PyObject* self, PyObject *args, PyObject* kwds)						\
	{																						\
		return static_cast<CLASS*>(self)->onScriptInit(self, args, kwds);					\
	}																						\
																							\
public:																						\
	/* 最终将要被安装到脚本模块中的方法和成员存放列表*/											\
	static PyMethodDef* _##CLASS##_lpScriptmethods;											\
	static PyMemberDef* _##CLASS##_lpScriptmembers;											\
	static PyGetSetDef* _##CLASS##_lpgetseters;												\
	/* 本模块所要暴漏给脚本的方法和成员， 最终会被导入到上面的2个指针列表中 */					\
	static PyMethodDef _##CLASS##_scriptMethods[];											\
	static PyMemberDef _##CLASS##_scriptMembers[];											\
	static PyGetSetDef _##CLASS##_scriptGetSeters[];										\
																							\
	static bool _##CLASS##_py_installed;													\
																							\
	/** getset的只读属性
	*/																						\
	static int __py_readonly_descr(PyObject* self, PyObject* value, void* closure)			\
	{																						\
		PyErr_Format(PyExc_TypeError,														\
		"Sorry, this attribute %s.%s is read-only", (self != NULL ? self->ob_type->tp_name\
		: #CLASS), (closure != NULL ? (char*)closure : "unknown"));							\
		PyErr_PrintEx(0);																	\
		return 0;																			\
	}																						\
																							\
	/** getset的只写属性
	*/																						\
	static int __py_writeonly_descr(PyObject* self, PyObject* value, void* closure)			\
	{																						\
		PyErr_Format(PyExc_TypeError,														\
		"Sorry, this attribute %s.%s is write-only", (self != NULL ? self->ob_type->tp_name\
		: #CLASS), (closure != NULL ? (char*)(closure) : "unknown"));						\
		PyErr_PrintEx(0);																	\
		return 0;																			\
	}																						\
																							\
	/** 这个接口可以获得当前模块的脚本类别 
	*/																						\
	static PyTypeObject* getScriptType(void)												\
	{																						\
		return &_scriptType;																\
	}																						\
	static PyTypeObject* getBaseScriptType(void)											\
	{																						\
		if(strcmp("ScriptObject", #SUPERCLASS) == 0)										\
			return 0;																		\
		return SUPERCLASS::getScriptType();													\
	}																						\
																							\
	static long calcDictOffset(void)														\
	{																						\
		if(strcmp("ScriptObject", #SUPERCLASS) == 0)										\
			return 0;																		\
		return 0;																			\
	}																						\
																							\
	/** 计算所有继承模块的暴露方法个数 
	*/																						\
	static int calcTotalMethodCount(void)													\
	{																						\
		int nlen = 0;																		\
		while(true)																			\
		{																					\
			PyMethodDef* pmf = &_##CLASS##_scriptMethods[nlen];								\
			if(!pmf->ml_doc && !pmf->ml_flags && !pmf->ml_meth && !pmf->ml_name)			\
				break;																		\
			nlen++;																			\
		}																					\
																							\
		if(strcmp(#CLASS, #SUPERCLASS) == 0)												\
			return nlen;																	\
		return SUPERCLASS::calcTotalMethodCount() + nlen;									\
	}																						\
																							\
	/** 计算所有继承模块的暴露成员个数 
	*/																						\
	static int calcTotalMemberCount(void)													\
	{																						\
		int nlen = 0;																		\
		while(true)																			\
		{																					\
			PyMemberDef* pmd = &_##CLASS##_scriptMembers[nlen];								\
			if(!pmd->doc && !pmd->flags && !pmd->type && !pmd->name && !pmd->offset)		\
				break;																		\
			nlen++;																			\
		}																					\
																							\
		if(strcmp(#CLASS, #SUPERCLASS) == 0)												\
			return nlen;																	\
		return SUPERCLASS::calcTotalMemberCount() + nlen;									\
	}																						\
																							\
	/** 计算所有继承模块的暴露getset个数 
	*/																						\
	static int calcTotalGetSetCount(void)													\
	{																						\
		int nlen = 0;																		\
		while(true)																			\
		{																					\
			PyGetSetDef* pgs = &_##CLASS##_scriptGetSeters[nlen];							\
			if(!pgs->doc && !pgs->get && !pgs->set && !pgs->name && !pgs->closure)			\
				break;																		\
			nlen++;																			\
		}																					\
																							\
		if(strcmp(#CLASS, #SUPERCLASS) == 0)												\
			return nlen;																	\
		return SUPERCLASS::calcTotalGetSetCount() + nlen;									\
	}																						\
																							\
	/** 将所有父类以及当前模块的暴露成员和方法安装到最终要导入脚本的列表中 
	*/																						\
	static void setupScriptMethodAndAttribute(PyMethodDef* lppmf, PyMemberDef* lppmd,		\
	PyGetSetDef* lppgs)																		\
	{																						\
		int i = 0;																			\
		PyMethodDef* pmf = NULL;															\
		PyMemberDef* pmd = NULL;															\
		PyGetSetDef* pgs = NULL;															\
																							\
		while(true){																		\
			pmf = &_##CLASS##_scriptMethods[i];												\
			if(!pmf->ml_doc && !pmf->ml_flags && !pmf->ml_meth && !pmf->ml_name)			\
				break;																		\
			i++;																			\
			*(lppmf++) = *pmf;																\
		}																					\
																							\
		i = 0;																				\
		while(true){																		\
			pmd = &_##CLASS##_scriptMembers[i];												\
			if(!pmd->doc && !pmd->flags && !pmd->type && !pmd->name && !pmd->offset)		\
				break;																		\
			i++;																			\
			*(lppmd++) = *pmd;																\
		}																					\
																							\
		i = 0;																				\
		while(true){																		\
			pgs = &_##CLASS##_scriptGetSeters[i];											\
			if(!pgs->doc && !pgs->get && !pgs->set && !pgs->name && !pgs->closure)			\
				break;																		\
			i++;																			\
			*(lppgs++) = *pgs;																\
		}																					\
																							\
		if(strcmp(#CLASS, #SUPERCLASS) == 0){												\
			*(lppmf) = *pmf;																\
			*(lppmd) = *pmd;																\
			*(lppgs) = *pgs;																\
			return;																			\
		}																					\
																							\
		SUPERCLASS::setupScriptMethodAndAttribute(lppmf, lppmd, lppgs);						\
	}																						\
																							\
	/** 安装当前脚本模块 
		@param mod: 所要导入的主模块
	*/																						\
	static void installScript(PyObject* mod, const char* name = #CLASS)						\
	{																						\
		int nMethodCount			= CLASS::calcTotalMethodCount();						\
		int nMemberCount			= CLASS::calcTotalMemberCount();						\
		int nGetSetCount			= CLASS::calcTotalGetSetCount();						\
																							\
		_##CLASS##_lpScriptmethods	= new PyMethodDef[nMethodCount + 2];					\
		_##CLASS##_lpScriptmembers	= new PyMemberDef[nMemberCount + 2];					\
		_##CLASS##_lpgetseters		= new PyGetSetDef[nGetSetCount + 2];					\
																							\
		setupScriptMethodAndAttribute(_##CLASS##_lpScriptmethods,							\
									  _##CLASS##_lpScriptmembers,							\
									  _##CLASS##_lpgetseters);								\
																							\
		_scriptType.tp_methods		= _##CLASS##_lpScriptmethods;							\
		_scriptType.tp_members		= _##CLASS##_lpScriptmembers;							\
		_scriptType.tp_getset		= _##CLASS##_lpgetseters;								\
																							\
																							\
		CLASS::onInstallScript(mod);														\
		if (PyType_Ready(&_scriptType) < 0){												\
			ERROR_MSG("PyType_Ready(" #CLASS ") error!");									\
			PyErr_Print();																	\
			return;																			\
		}																					\
																							\
		if(mod)																				\
		{																					\
			Py_INCREF(&_scriptType);														\
			if(PyModule_AddObject(mod, name, (PyObject *)&_scriptType) < 0)					\
			{																				\
				ERROR_MSG(fmt::format("PyModule_AddObject({}) error!", name));				\
			}																				\
		}																					\
																							\
		SCRIPT_ERROR_CHECK();																\
		_##CLASS##_py_installed = true;														\
																							\
		ScriptObject::scriptObjectTypes[name] = &_scriptType;								\
	}																						\
																							\
	/** 卸载当前脚本模块 
	*/																						\
	static void uninstallScript(void)														\
	{																						\
		SAFE_RELEASE_ARRAY(_##CLASS##_lpScriptmethods);										\
		SAFE_RELEASE_ARRAY(_##CLASS##_lpScriptmembers);										\
		SAFE_RELEASE_ARRAY(_##CLASS##_lpgetseters);											\
		CLASS::onUninstallScript();															\
																							\
		if(_##CLASS##_py_installed)															\
			Py_DECREF(&_scriptType);														\
	}																						\




/** 这个宏正式的初始化一个脚本模块， 将一些必要的信息填充到python的type对象中.
add by huyf 2019.08.20:EntityCall.EntityCallAbstract.EntityRemoteMethod(base).EntityRemoteMethod(cell)
*/
#define SCRIPT_INIT(CLASS, CALL, SEQ, MAP, ITER, ITERNEXT)									\
		TEMPLATE_SCRIPT_INIT(;,CLASS, CLASS, CALL, SEQ, MAP, ITER, ITERNEXT)				\


#define TEMPLATE_SCRIPT_INIT(TEMPLATE_HEADER, TEMPLATE_CLASS,								\
	CLASS, CALL, SEQ, MAP, ITER, ITERNEXT)													\
	TEMPLATE_HEADER PyMethodDef* TEMPLATE_CLASS::_##CLASS##_lpScriptmethods = NULL;			\
	TEMPLATE_HEADER PyMemberDef* TEMPLATE_CLASS::_##CLASS##_lpScriptmembers = NULL;			\
	TEMPLATE_HEADER PyGetSetDef* TEMPLATE_CLASS::_##CLASS##_lpgetseters = NULL;				\
																							\
	TEMPLATE_HEADER																			\
	PyTypeObject TEMPLATE_CLASS::_scriptType =												\
	{																						\
		PyVarObject_HEAD_INIT(&PyType_Type, 0)												\
		#CLASS,													/* tp_name            */	\
		sizeof(TEMPLATE_CLASS),									/* tp_basicsize       */	\
		0,														/* tp_itemsize        */	\
		(destructor)TEMPLATE_CLASS::_tp_dealloc,				/* tp_dealloc         */	\
		(printfunc)TEMPLATE_CLASS::_tp_print,					/* huyf tp_print      */	\
		0,														/* tp_getattr         */	\
		0,														/* tp_setattr         */	\
		0,														/* tp_compare         */	\
		TEMPLATE_CLASS::_tp_repr,								/* tp_repr            */	\
		0,														/* tp_as_number       */	\
		SEQ,													/* tp_as_sequence     */	\
		MAP,													/* tp_as_mapping      */	\
		0,														/* tp_hash            */	\
		CALL,													/* tp_call            */	\
		TEMPLATE_CLASS::_tp_str,								/* tp_str             */	\
		(getattrofunc)CLASS::_tp_getattro,						/* tp_getattro        */	\
		(setattrofunc)CLASS::_tp_setattro,						/* tp_setattro        */	\
		0,														/* tp_as_buffer       */	\
		Py_TPFLAGS_DEFAULT | Py_TPFLAGS_BASETYPE,				/* tp_flags           */	\
		"KBEngine::" #CLASS " objects.",						/* tp_doc             */	\
		0,														/* tp_traverse        */	\
		0,														/* tp_clear           */	\
		0,														/* tp_richcompare     */	\
		0,														/* tp_weaklistoffset  */	\
		(getiterfunc)ITER,										/* tp_iter            */	\
		(iternextfunc)ITERNEXT,									/* tp_iternext        */	\
		0,														/* tp_methods         */	\
		0,														/* tp_members         */	\
		0,														/* tp_getset          */	\
		TEMPLATE_CLASS::getBaseScriptType(),					/* tp_base            */	\
		0,														/* tp_dict            */	\
		0,														/* tp_descr_get       */	\
		0,														/* tp_descr_set       */	\
		TEMPLATE_CLASS::calcDictOffset(),						/* tp_dictoffset      */	\
		(initproc)TEMPLATE_CLASS::_tp_init,						/* tp_init            */	\
		0,														/* tp_alloc           */	\
		TEMPLATE_CLASS::_tp_new,								/* tp_new             */	\
		PyObject_GC_Del,										/* tp_free            */	\
	};																						\

// BASE_SCRIPT_HREADER基础类脚本初始化, 该类由脚本继承
#define BASE_SCRIPT_INIT(CLASS, CALL, SEQ, MAP, ITER, ITERNEXT)								\
	PyMethodDef* CLASS::_##CLASS##_lpScriptmethods = NULL;									\
	PyMemberDef* CLASS::_##CLASS##_lpScriptmembers = NULL;									\
	PyGetSetDef* CLASS::_##CLASS##_lpgetseters = NULL;										\
																							\
	PyTypeObject CLASS::_scriptType =														\
	{																						\
		PyVarObject_HEAD_INIT(NULL, 0)														\
		#CLASS,													/* tp_name            */	\
		sizeof(CLASS),											/* tp_basicsize       */	\
		0,														/* tp_itemsize        */	\
		(destructor)CLASS::_tp_dealloc,							/* tp_dealloc         */	\
		(printfunc)CLASS::_tp_print,							/* huyf tp_print      */	\
		0,														/* tp_getattr         */	\
		0,														/* tp_setattr         */	\
		0,														/* void *tp_reserved  */	\
		CLASS::_tp_repr,										/* tp_repr            */	\
		0,														/* tp_as_number       */	\
		SEQ,													/* tp_as_sequence     */	\
		MAP,													/* tp_as_mapping      */	\
		0,														/* tp_hash            */	\
		CALL,													/* tp_call            */	\
		CLASS::_tp_str,											/* tp_str             */	\
		(getattrofunc)CLASS::_tp_getattro,						/* tp_getattro        */	\
		(setattrofunc)CLASS::_tp_setattro,						/* tp_setattro        */	\
		0,														/* tp_as_buffer       */	\
		Py_TPFLAGS_DEFAULT | Py_TPFLAGS_BASETYPE,				/* tp_flags           */	\
		0,														/* tp_doc             */	\
		0,														/* tp_traverse        */	\
		0,														/* tp_clear           */	\
		0,														/* tp_richcompare     */	\
		0,														/* tp_weaklistoffset  */	\
		(getiterfunc)ITER,										/* tp_iter            */	\
		(iternextfunc)ITERNEXT,									/* tp_iternext        */	\
		0,														/* tp_methods         */	\
		0,														/* tp_members         */	\
		0,														/* tp_getset          */	\
		CLASS::getBaseScriptType(),								/* tp_base            */	\
		0,														/* tp_dict            */	\
		0,														/* tp_descr_get       */	\
		0,														/* tp_descr_set       */	\
		CLASS::calcDictOffset(),								/* tp_dictoffset      */	\
		0,														/* tp_init            */	\
		0,														/* tp_alloc           */	\
		0,														/* tp_new             */	\
		PyObject_GC_Del,										/* tp_free            */	\
		0,														/* tp_is_gc           */	\
		0,														/* tp_bases           */	\
		0,														/* tp_mro             */	\
		0,														/* tp_cache           */	\
		0,														/* tp_subclasses      */	\
		0,														/* tp_weaklist        */	\
		0,														/* tp_del			  */	\
	};																						\


class ScriptObject: public PyObject
{
	/** 
		子类化 将一些py操作填充进派生类 
	*/
	SCRIPT_OBJECT_HREADER(ScriptObject, ScriptObject)							
public:	
	ScriptObject(PyTypeObject* pyType, bool isInitialised = false);
	~ScriptObject();

	// 所有的kbe脚本类别
	typedef KBEUnordered_map<std::string, PyTypeObject*> SCRIPTOBJECT_TYPES;
	static SCRIPTOBJECT_TYPES scriptObjectTypes;
	static PyTypeObject* getScriptObjectType(const std::string& name);

	/** 
		脚本对象引用计数 
	*/
	void incRef() const				{ 
										Py_INCREF((PyObject*)this);
										if (strcmp(scriptName(), "CNpc") == 0)
										{
											DEBUG_MSG(fmt::format("huyf-yh: ScriptObject::incRef p={:p} ScriptModule={} nRefCount={}\n", (void*)this, ob_type->tp_name, int(((PyObject*)this)->ob_refcnt)));
										}
									}
	void decRef() const				{ 
										Py_DECREF((PyObject*)this); 
										if (strcmp(scriptName(), "CNpc") == 0)
										{
											DEBUG_MSG(fmt::format("huyf-yh: ScriptObject::decRef p={:p} ScriptModule={} nRefCount={}\n", (void*)this, ob_type->tp_name, int(((PyObject*)this)->ob_refcnt)));
										}
									}

	int refCount() const			{ return int(((PyObject*)this)->ob_refcnt); }
	
	/** 
		获得对象的描述 
	*/
	PyObject* tp_repr();
	PyObject* tp_str();

	DECLARE_PY_GET_MOTHOD(py__module__);
	DECLARE_PY_GET_MOTHOD(py__qualname__);
	DECLARE_PY_GET_MOTHOD(py__name__);

	/** 
		脚本请求创建一个该对象 
	*/
	static PyObject* tp_new(PyTypeObject* type, PyObject* args, 
		PyObject* kwds);

	/** 
		脚本请求获取属性或者方法 
	*/
	PyObject* onScriptGetAttribute(PyObject* attr);						

	/** 
		脚本请求设置属性或者方法 
	*/
	int onScriptSetAttribute(PyObject* attr, PyObject* value);			

	/** 
		脚本请求删除一个属性 
	*/
	int onScriptDelAttribute(PyObject* attr);

	/** 
		脚本请求初始化 
	*/
	int onScriptInit(PyObject* self, PyObject *args, 
		PyObject* kwds);

	/** 
		获取对象类别名称
	*/
	const char* scriptName() const { return ob_type->tp_name; }

	/** 
		脚本被安装时被调用 
	*/
	static void onInstallScript(PyObject* mod) {}

	/** 
		脚本被卸载时被调用 
	*/
	static void onUninstallScript() {}
} ;

#define SWALLOWER_OBJECT																\
typedef struct																			\
{																						\
	PyObject_HEAD;																		\
} SwallowerObject;																		\
																						\
static PyObject * swallowerCall(SwallowerObject* self, PyObject *args, PyObject *kwds)	\
{																						\
	S_Return;																			\
}																						\
static PyObject * swallowerGetAttrib(SwallowerObject* self, char* name)					\
{																						\
	Py_INCREF(self);																	\
	return (PyObject *)self;															\
}																						\
																						\
static void swallowerDealloc(SwallowerObject* self)										\
{																						\
	return;																				\
}																						\
																						\
static int swallowerAsBool(SwallowerObject* self)										\
{																						\
	return false;																		\
}																						\
PyNumberMethods numMethods = {															\
	0,									/*binaryfunc nb_add;*/							\
	0,									/*binaryfunc nb_subtract;*/						\
	0,									/*binaryfunc nb_multiply;*/						\
	/*ScriptVector4::py_divide,*/			/*binaryfunc nb_divide;*/					\
	0,									/*binaryfunc nb_remainder;*/					\
	0,									/*binaryfunc nb_divmod;*/						\
	0,									/*ternaryfunc nb_power;*/						\
	0,									/*unaryfunc nb_negative;*/						\
	0,									/*unaryfunc nb_positive;*/						\
	0,									/*unaryfunc nb_absolute;*/						\
	(inquiry)swallowerAsBool,			/*inquiry nb_nonzero*/							\
};																						\
PyTypeObject PySwallower_Type = {														\
	PyObject_HEAD_INIT(&PyType_Type)													\
	"Swallower",																		\
	sizeof(SwallowerObject),															\
	0,																					\
	(destructor)swallowerDealloc,				/* tp_dealloc */						\
	0,											/* tp_print */							\
	(getattrfunc)swallowerGetAttrib,            /* tp_getattr */						\
	0,                                          /* tp_setattr */						\
	0,                                          /* tp_reserved */						\
	0,                                          /* tp_repr */							\
	&numMethods,                                /* tp_as_number */						\
	0,                                          /* tp_as_sequence */					\
	0,                                          /* tp_as_mapping */						\
	0,                                          /* tp_hash */							\
	(ternaryfunc)swallowerCall,                 /* tp_call */							\
	0,                                          /* tp_str */							\
	0,                                          /* tp_getattro */						\
	0,                                          /* tp_setattro */						\
	0,                                          /* tp_as_buffer */						\
	Py_TPFLAGS_DEFAULT,                         /* tp_flags */							\
	0,                                          /* tp_doc */							\
	0,											/* tp_traverse */						\
	0,											/* tp_clear */							\
	0,											/* tp_richcompare */					\
	0,                                          /* tp_weaklistoffset */					\
	0,                                          /* tp_iter */							\
	0,                                          /* tp_iternext */						\
	0,                                          /* tp_methods */						\
	0,                                          /* tp_members */						\
	0,                                          /* tp_getset */							\
	&PyBaseObject_Type,                         /* tp_base */							\
	0,                                          /* tp_dict */							\
	0,                                          /* tp_descr_get */						\
	0,                                          /* tp_descr_set */						\
	0,                                          /* tp_dictoffset */						\
	0,											/* tp_init */							\
	PyType_GenericAlloc,                        /* tp_alloc */							\
	PyType_GenericNew,                          /* tp_new */							\
	0,											/* tp_free */							\
};																						\
static PyObject * swallower = PySwallower_Type.tp_alloc(&PySwallower_Type, 0);			\

}
}


#ifdef CODE_INLINE
#include "scriptobject.inl"
#endif

#endif // KBE_SCRIPTOBJECT_H
