#ifndef _INCLUDE_VHOOK_H_
#define _INCLUDE_VHOOK_H_

#include "extension.h"
#include <sourcehook.h>
#include <macro-assembler-x86.h>

enum MRESReturn
{
	MRES_ChangedHandled = -2,	// Use changed values and return MRES_Handled
	MRES_ChangedOverride,		// Use changed values and return MRES_Override
	MRES_Ignored,				// plugin didn't take any action
	MRES_Handled,				// plugin did something, but real function should still be called
	MRES_Override,				// call real function, but use my return value
	MRES_Supercede				// skip real function; use my return value
};

enum HookParamType
{
	HookParamType_Unknown,
	HookParamType_Int,
	HookParamType_Bool,
	HookParamType_Float,
	HookParamType_String,
	HookParamType_StringPtr,
	HookParamType_CharPtr,
	HookParamType_VectorPtr,
	HookParamType_CBaseEntity,
	HookParamType_ObjectPtr,
	HookParamType_Edict
};

enum ReturnType
{
	ReturnType_Unknown,
	ReturnType_Void,
	ReturnType_Int,
	ReturnType_Bool,
	ReturnType_Float,
	ReturnType_String,
	ReturnType_StringPtr,
	ReturnType_CharPtr,
	ReturnType_Vector,
	ReturnType_VectorPtr,
	ReturnType_CBaseEntity,
	ReturnType_Edict
};

enum ThisPointerType
{
	ThisPointer_Ignore,
	ThisPointer_CBaseEntity,
	ThisPointer_Address
};

enum HookType
{
	HookType_Entity,
	HookType_GameRules,
	HookType_Raw
};

struct ParamInfo
{
	HookParamType type;
	size_t size;
	unsigned int flag;
	SourceHook::PassInfo::PassType pass_type;
};

class HookReturnStruct
{
public:
	~HookReturnStruct()
	{
		if(this->isChanged && this->newResult)
		{
			if(this->type == ReturnType_CharPtr)
			{
				delete (char *)this->newResult;
			}
			else if(this->type == ReturnType_VectorPtr)
			{
				delete (Vector *)this->newResult;
			}
			else if(this->type == ReturnType_Float)
			{
				if(this->orgResult)
				{
					free(this->orgResult);
				}
				free(this->newResult);
			}
		}
	}
public:
	ReturnType type;
	bool isChanged;
	void *orgResult;
	void *newResult;
};

class DHooksInfo
{
public:
	CUtlVector<ParamInfo> params;
	int offset;
	unsigned int returnFlag;
	ReturnType returnType;
	bool post;
	IPluginFunction *plugin_callback;
	int entity;
	ThisPointerType thisType;
	HookType hookType;
};

class DHooksCallback : public SourceHook::ISHDelegate, public DHooksInfo
{
public:
    virtual bool IsEqual(ISHDelegate *pOtherDeleg){return false;};
    virtual void DeleteThis()
	{
		*(void ***)this = this->oldvtable;
		g_pSM->GetScriptingEngine()->FreePageMemory(this->newvtable[2]);
		delete this->newvtable;
		delete this;
	};
	virtual void Call() {};
public:
	void **newvtable;
	void **oldvtable;
};

#ifndef __linux__
void *Callback(DHooksCallback *dg, void **stack, size_t *argsizep);
//float Callback_float(DHooksCallback *dg, void **stack, size_t *argsizep);
#else
void *Callback(DHooksCallback *dg, void **stack);
float Callback_float(DHooksCallback *dg, void **stack);
#endif
bool SetupHookManager(ISmmAPI *ismm);
void CleanupHooks(IPluginContext *pContext);
size_t GetParamTypeSize(HookParamType type);
SourceHook::PassInfo::PassType GetParamTypePassType(HookParamType type);

#ifdef  __linux__
static void *GenerateThunk(ReturnType type)
{
	MacroAssemblerX86 masm;
 
	masm.push(ebp);
	masm.movl(ebp, esp);
	masm.lea(eax, Operand(ebp, 12));
	masm.push(eax);
	masm.push(Operand(ebp, 8));
	/*if(type == ReturnType_Float)
		masm.call(ExternalAddress((void *)Callback_float));
	else*/
		masm.call(ExternalAddress((void *)Callback));
	masm.addl(esp, 8);
	masm.pop(ebp);
	masm.ret();
 
	void *base =  g_pSM->GetScriptingEngine()->AllocatePageMemory(masm.length());
	masm.emitToExecutableMemory(base);
	return base;
}
#else
static void *GenerateThunk(ReturnType type)
{
	MacroAssemblerX86 masm;
 
	masm.push(ebp);
	masm.movl(ebp, esp);
	masm.subl(esp, 4);
	masm.push(esp);
	masm.lea(eax, Operand(ebp, 8));
	masm.push(eax);
	masm.push(ecx);
	//if(type == ReturnType_Float)
	//	masm.call(ExternalAddress(Callback_float));
	//else
		masm.call(ExternalAddress(Callback));
	masm.addl(esp, 12);
	masm.pop(ecx); // grab arg size
	masm.pop(ebp); // restore ebp
	masm.pop(edx); // grab return address in edx
	masm.addl(esp, ecx); // remove arguments
	masm.jmp(edx); // return to caller
 
	void *base =  g_pSM->GetScriptingEngine()->AllocatePageMemory(masm.length());
	masm.emitToExecutableMemory(base);
	return base;
}
#endif

static DHooksCallback *MakeHandler(ReturnType type)
{
	DHooksCallback *dg = new DHooksCallback();
	dg->returnType = type;
	dg->oldvtable = *(void ***)dg;
	dg->newvtable = new void *[3];
	dg->newvtable[0] = dg->oldvtable[0];
	dg->newvtable[1] = dg->oldvtable[1];
	dg->newvtable[2] = GenerateThunk(type);
	*(void ***)dg = dg->newvtable;
	return dg;
}

class HookParamsStruct
{
public:
	HookParamsStruct()
	{
		this->orgParams = NULL;
		this->newParams = NULL;
		this->dg = NULL;
		this->isChanged = NULL;
	}
	~HookParamsStruct()
	{
		if(this->orgParams != NULL)
		{
			free(this->orgParams);
		}
		if(this->isChanged != NULL)
		{
			free(this->isChanged);
		}
		if(this->newParams != NULL)
		{
			for(int i = dg->params.Count() - 1; i >= 0 ; i--)
			{
				if(this->newParams[i] == NULL)
					continue;

				if(dg->params.Element(i).type == HookParamType_VectorPtr)
				{
					delete (Vector *)this->newParams[i];
				}
				else if(dg->params.Element(i).type == HookParamType_CharPtr)
				{
					delete (char *)this->newParams[i];
				}
			}
			free(this->newParams);
		}
	}
public:
	void **orgParams;
	void **newParams;
	bool *isChanged;
	DHooksCallback *dg;
};

class HookSetup
{
public:
	HookSetup(ReturnType returnType, unsigned int returnFlag, HookType hookType, ThisPointerType thisType, int offset, IPluginFunction *callback)
	{
		this->returnType = returnType;
		this->returnFlag = returnFlag;
		this->hookType = hookType;
		this->thisType = thisType;
		this->offset = offset;
		this->callback = callback;
	};
	~HookSetup(){};
public:
	unsigned int returnFlag;
	ReturnType returnType;
	HookType hookType;
	ThisPointerType thisType;
	CUtlVector<ParamInfo> params;
	int offset;
	IPluginFunction *callback;
};

class DHooksManager
{
public:
	DHooksManager(HookSetup *setup, void *iface, IPluginFunction *remove_callback, bool post);
	~DHooksManager()
	{
		if(this->hookid)
		{
			g_SHPtr->RemoveHookByID(this->hookid);
			if(this->remove_callback)
			{
				this->remove_callback->PushCell(this->hookid);
				this->remove_callback->Execute(NULL);
			}
		}
	}
public:
	int hookid;
	DHooksCallback *callback;
	IPluginFunction *remove_callback;
};

size_t GetStackArgsSize(DHooksCallback *dg);

extern IBinTools *g_pBinTools;
extern HandleType_t g_HookParamsHandle;
extern HandleType_t g_HookReturnHandle;
#endif
