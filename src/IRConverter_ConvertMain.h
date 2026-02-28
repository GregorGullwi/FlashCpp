// IRConverter_ConvertMain.h - IrToObjConverter template class definition
// Part of IRConverter.h unity build - do not add #pragma once

template<class TWriterClass = ObjectFileWriter>
class IrToObjConverter {
public:
	IrToObjConverter() = default;
	
	~IrToObjConverter() = default;

#include "IRConverter_Conv_Convert.h"

private:
#include "IRConverter_Conv_CorePrivate.h"

#include "IRConverter_Conv_Calls.h"

#include "IRConverter_Conv_VarDecl.h"

#include "IRConverter_Conv_Arithmetic.h"

#include "IRConverter_Conv_ControlFlow.h"

#include "IRConverter_Conv_Memory.h"

#include "IRConverter_Conv_EHSeh.h"

#include "IRConverter_Conv_Fields.h"

}; // End of IrToObjConverter class
