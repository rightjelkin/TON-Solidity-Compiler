/*
 * Copyright 2018-2019 TON DEV SOLUTIONS LTD.
 *
 * Licensed under the  terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License.
 *
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the  GNU General Public License for more details at: https://www.gnu.org/licenses/gpl-3.0.html
 */
/**
 * @author TON Labs <connect@tonlabs.io>
 * @date 2019
 */

#include "libsolutil/picosha2.h"

#include "TVMABI.hpp"
#include "TVMPusher.hpp"
#include "TVMStructCompiler.hpp"
#include "TVMExpressionCompiler.hpp"

using namespace solidity::frontend;

void TVMABI::generateABI(ContractDefinition const *contract, std::vector<PragmaDirective const *> const &pragmaDirectives,
						ostream *out) {

	PragmaDirectiveHelper pdh{pragmaDirectives};
	TVMCompilerContext ctx(contract, pdh);

	std::vector<const FunctionDefinition *> publicFunctions {};
	std::vector<const EventDefinition *> events {};

	if (auto main_constr = contract->constructor(); main_constr != nullptr)
		publicFunctions.push_back(contract->constructor());

	for (auto c : contract->annotation().linearizedBaseContracts) {
		for (const auto &_function : c->definedFunctions()) {
			if (_function->isPublic() && !isTvmIntrinsic(_function->name()) && !_function->isConstructor() &&
				!_function->isReceive() && !_function->isFallback() && !_function->isOnBounce() && !_function->isOnTickTock())
				publicFunctions.push_back(_function);
		}
	}

	for (const auto &_event : contract->interfaceEvents())
		events.push_back(_event);

	std::set<std::string> used;

	Json::Value root(Json::objectValue);
	root["ABI version"] = ctx.pragmaHelper().abiVersion();

	// header
	if (ctx.pragmaHelper().abiVersion() == 2) {
		Json::Value header(Json::arrayValue);
		for (const std::string &h : {"pubkey", "time", "expire"}) {
			if (std::get<0>(pdh.haveHeader(h)) || (h == "time" && ctx.haveTimeInAbiHeader())) {
				header.append(h);
			}
		}
		root["header"] = header;
	}

	// functions
	{
		Json::Value functions(Json::arrayValue);
		for (FunctionDefinition const* f : publicFunctions) {
			auto fname = TVMCompilerContext::getFunctionExternalName(f);
			if (used.count(fname)) {
				continue;
			}
			used.insert(fname);
			functions.append(processFunction(fname, f->parameters(), f->returnParameters(), f));
		}

		if (used.count("constructor") == 0) {
			auto v = ast_vec<VariableDeclaration>();
			functions.append(processFunction("constructor", v, v, nullptr));
		}

		root["functions"] = functions;
	}

	// events
	{
		Json::Value eventAbi(Json::arrayValue);
		std::set<std::string> usedEvents;
		for (const auto &e: events) {
			const auto &ename = e->name();
			solAssert(!ename.empty(), "Empty event name!");
			if (usedEvents.count(ename)) {
				solAssert(false, "Event name duplication!");
			}
			usedEvents.insert(ename);
			Json::Value cur;
			cur["name"] = ename;
			cur["inputs"] = encodeParams(e->parameters());
			eventAbi.append(cur);
		}
		root["events"] = eventAbi;
	}

	// data
	{
		int shift = 0;
		Json::Value data(Json::arrayValue);
		for (VariableDeclaration const* v : contract->stateVariables()) {
			if (v->isPublic()) {
				Json::Value cur;
				cur["key"] = TvmConst::C4::PersistenceMembersStartIndex + shift++;
				cur["name"] = v->name();
				cur["type"] = getParamTypeString(v->type(), *v);
				data.append(cur);
			}
		}
		root["data"] = data;
	}

//		Json::StreamWriterBuilder builder;
//		const std::string json_file = Json::writeString(builder, root);
//		*out << json_file << std::endl;

	*out << "{\n";
	*out << "\t" << R"("ABI version": )" << root["ABI version"] << ",\n";

	if (ctx.pragmaHelper().abiVersion() == 2) {
		*out << "\t" << R"("header": [)";
		for (unsigned i = 0; i < root["header"].size(); ++i) {
			*out << root["header"][i];
			if (i + 1 != root["header"].size()) {
				*out << ", ";
			}
		}
		*out << "],\n";
	}

	*out << "\t" << R"("functions": [)" << "\n";
	print(root["functions"], out);
	*out << "\t" << "],\n";

	*out << "\t" << R"("data": [)" << "\n";
	printData(root["data"], out);
	*out << "\t" << "],\n";

	*out << "\t" << R"("events": [)" << "\n";
	print(root["events"], out);
	*out << "\t" << "]\n";

	*out << "}" << endl;
}

void TVMABI::printData(const Json::Value &json, std::ostream* out) {
	for (unsigned f = 0; f < json.size(); ++f) {
		const auto &element = json[f];
		*out << "\t\t";

		Json::StreamWriterBuilder builder;
		builder["indentation"] = "";
		std::unique_ptr<Json::StreamWriter> writer(builder.newStreamWriter());
		writer->write(element, out);

		if (f + 1 != json.size())
			*out << ",";
		*out << std::endl;
	}
}

void TVMABI::print(const Json::Value &json, ostream *out) {
	for (unsigned f = 0; f < json.size(); ++f) {
		const auto& function = json[f];
		*out << "\t\t{\n";

		*out << "\t\t\t" << R"("name": )" << function["name"] << ",\n";

		if (function.isMember("id")) {
			*out << "\t\t\t" << R"("id": )" << function["id"] << ",\n";
		}

		*out << "\t\t\t" << R"("inputs": [)" << "\n";
		for (unsigned i = 0; i < function["inputs"].size(); ++i) {
			const auto& input = function["inputs"][i];
			Json::StreamWriterBuilder builder;
			builder["indentation"] = "";
			std::unique_ptr<Json::StreamWriter> writer(builder.newStreamWriter());
			*out << "\t\t\t\t";
			writer->write(input, out);
			if (i + 1 == function["inputs"].size()) {
				*out << "\n";
			} else {
				*out << ",\n";
			}
		}
		*out << "\t\t\t" << "]" << ",\n";

		*out << "\t\t\t" << R"("outputs": [)" << "\n";
		for (unsigned o = 0; o < function["outputs"].size(); ++o) {
			const auto& output = function["outputs"][o];
			Json::StreamWriterBuilder builder;
			builder["indentation"] = "";
			std::unique_ptr<Json::StreamWriter> writer(builder.newStreamWriter());
			*out << "\t\t\t\t";
			writer->write(output, out);
			if (o + 1 == function["outputs"].size()) {
				*out << "\n";
			} else {
				*out << ",\n";
			}
		}
		*out << "\t\t\t" << "]" << "\n";

		if (f + 1 == json.size())
			*out << "\t\t}\n";
		else
			*out << "\t\t},\n";
	}
}

Json::Value TVMABI::processFunction(const string &fname, const ast_vec<VariableDeclaration> &params,
									const ast_vec<VariableDeclaration> &retParams, FunctionDefinition const* funcDef) {
	Json::Value function;
	Json::Value inputs  = encodeParams(params);
	Json::Value outputs = encodeParams(retParams);
	function["name"] = fname;
	if (funcDef && (funcDef->functionID() != 0)) {
		std::ostringstream oss;
		oss << "0x" << std::hex << std::uppercase << funcDef->functionID();
		function["id"] = oss.str();
	}
	function["inputs"] = inputs;
	function["outputs"] = outputs;
	return function;
}

Json::Value TVMABI::encodeParams(const ast_vec<VariableDeclaration> &params) {
	Json::Value result(Json::arrayValue);
	size_t idx = 0;
	for (const auto& variable: params) {
		string name = variable->name();
		if (name.empty()) name = "value" + toString(idx);
		Json::Value json = setupType(name, getType(variable.get()), *variable);
		result.append(json);
		idx++;
	}
	return result;
}

string TVMABI::getParamTypeString(Type const *type, ASTNode const &node) {
	const Type::Category category = type->category();
	TypeInfo ti(type);
	if (category == Type::Category::Address || category == Type::Category::Contract) {
		return "address";
	} else if (ti.isNumeric) {
		if (to<BoolType>(type)) {
			return "bool";
		}
		if (ti.isSigned) {
			return "int" + toString(ti.numBits);
		} else {
			return "uint" + toString(ti.numBits);
		}
	} else if (auto arrayType = to<ArrayType>(type)) {
		Type const *arrayBaseType = arrayType->baseType();
		if (arrayType->isByteArray()) {
			return "bytes";
		}
		if (isIntegralType(arrayBaseType) || isAddressOrContractType(arrayBaseType) ||
			to<StructType>(arrayBaseType) || to<ArrayType>(arrayBaseType)) {

			return getParamTypeString(arrayBaseType, node) + "[]";
		}
		cast_error(node, "Unsupported param type " + type->toString(true));
	} else if (to<StructType>(type)) {
		return "tuple";
	} else if (category == Type::Category::TvmCell) {
		return "cell";
	} else if (category == Type::Category::Mapping) {
		auto mapping = to<MappingType>(type);
		return "map(" + getParamTypeString(mapping->keyType(), node) + "," +
			   getParamTypeString(mapping->valueType(), node) + ")";
	}
	cast_error(node, "Unsupported param type " + type->toString(true));
}

Json::Value TVMABI::setupType(const string &name, const Type *type, ASTNode const &node) {
	Json::Value json(Json::objectValue);
	json["name"] = name;
	json["type"] = getParamTypeString(type, node);

	auto setupComponents = [&](Type const* type){
		json["components"] = setupStructComponents(to<StructType>(type), node);
	};

	switch (type->category()) {
		case Type::Category::Struct:
			setupComponents(type);
			break;
		case Type::Category::Array: {
			auto arrayType = to<ArrayType>(type);
			Type const *arrayBaseType = arrayType->baseType();
			if (arrayBaseType->category() == Type::Category::Struct) {
				setupComponents(arrayBaseType);
			}
			break;
		}
		case Type::Category::Mapping: {
			auto mappingType = to<MappingType>(type);
			Type const *valueType = mappingType->valueType();
			if (valueType->category() == Type::Category::Struct) {
				setupComponents(valueType);
			}
			break;
		}
		default:
			break;
	}
	return json;
}

Json::Value TVMABI::setupStructComponents(const StructType *type, ASTNode const &node) {
	Json::Value components(Json::arrayValue);
	const StructDefinition& structDefinition = type->structDefinition();
	const auto& members = structDefinition.members();
	for (const auto & member : members) {
		components.append(setupType(member->name(), getType(member.get()), node));
	}
	return components;
}

DecodePositionAbiV1::DecodePositionAbiV1() :
		isPositionValid{true},
		minRestSliceBits{TvmConst::CellBitLength - TvmConst::Message::functionIdLength - TvmConst::Message::timestampLength},
		maxRestSliceBits{TvmConst::CellBitLength - TvmConst::Message::functionIdLength},
		minUsedRef{0},
		maxUsedRef{1}
{

}

DecodePosition::Algo DecodePositionAbiV1::updateStateAndGetLoadAlgo(Type const *type) {
	ABITypeSize size(type);
	solAssert(0 <= size.minRefs && size.minRefs <= 1, "");
	solAssert(0 <= size.maxRefs && size.maxRefs <= 1, "");
	solAssert(0 <= size.minBits && size.minBits <= size.maxBits, "");

	if (!isPositionValid) {
		return Unknown;
	}

	minRestSliceBits -= size.maxBits;
	maxRestSliceBits -= size.minBits;
	minUsedRef += size.minRefs;
	maxUsedRef += size.maxRefs;

	if (minRestSliceBits < 0 && maxRestSliceBits >= 0) {
		isPositionValid = false;
		return Unknown;
	}

	if (maxUsedRef == 4 && maxUsedRef != minUsedRef) {
		isPositionValid = false;
		return Unknown;
	}

	if (minRestSliceBits < 0 || maxUsedRef == 4) {
		minRestSliceBits = TvmConst::CellBitLength - size.maxBits;
		maxRestSliceBits = TvmConst::CellBitLength - size.minBits;
		minUsedRef = size.minRefs;
		maxUsedRef = size.maxRefs;
		return LoadNextCell;
	}

	return JustLoad;
}

Position::Position(int usedBits, int usedRefs) : usedBits{usedBits}, usedRefs{usedRefs} {

}

void Position::update(const int bits, const int refs) {
	usedBits += bits;
	usedRefs += refs;
	if ((bits > 0 && usedBits > TvmConst::CellBitLength) || (refs > 0 && usedRefs >= 4)) {
		usedBits = bits;
		usedRefs = refs;
		++idCell;
	}
}

void Position::loadRef() {
	++usedRefs;
	if (usedRefs == 4) {
		usedBits = 0;
		usedRefs = 1;
		++idCell;
	}
	solAssert(usedRefs <= 3, "");
}

int Position::cellNumber() const {
	return idCell;
}

DecodePositionAbiV2::DecodePositionAbiV2(int minBits, int maxBits, const ast_vec<VariableDeclaration> &params) :
		minPos{minBits, 0},
		maxPos{maxBits, 0} {
	for (const auto & param : params) {
		initTypes(param.get());
	}
	for (int i = 0; i < static_cast<int>(types.size()); ++i) {
		Type const* t = types[i];
		if (isRefType(t)) {
			lastRefType = i;
		}
	}
}

void DecodePositionAbiV2::initTypes(VariableDeclaration const *variable) {
	if (variable->type()->category() == Type::Category::Struct) {
		auto members = to<StructType>(variable->type())->structDefinition().members();
		for (const auto &m : members) {
			initTypes(m.get());
		}
	} else {
		types.push_back(variable->type());
	}
}

DecodePosition::Algo DecodePositionAbiV2::updateStateAndGetLoadAlgo(Type const *type) {
	++curTypeIndex;
	solAssert(type->toString() == types[curTypeIndex]->toString(), "");
	ABITypeSize size{type};
	solAssert(0 <= size.minRefs && size.minRefs <= 1, "");
	solAssert(0 <= size.maxRefs && size.maxRefs <= 1, "");
	solAssert(0 <= size.minBits && size.minBits <= size.maxBits, "");

	if (curTypeIndex == lastRefType) {
		if (curTypeIndex + 1 == static_cast<int>(types.size())) {
			minPos.loadRef();
			maxPos.loadRef();
			return JustLoad;
		} else {
			int prevMinCellNumber = minPos.cellNumber();
			minPos.loadRef();
			maxPos.loadRef();
			if (prevMinCellNumber == minPos.cellNumber() && minPos.cellNumber() == maxPos.cellNumber()) {
				return JustLoad;
			}
			return CheckBitsAndRefs;
		}
	}

	int prevMinCellNumber = minPos.cellNumber();
	int prevMaxCellNumber = maxPos.cellNumber();
	minPos.update(size.minBits, size.minRefs);
	maxPos.update(size.maxBits, size.maxRefs);

	if (prevMinCellNumber == minPos.cellNumber() && minPos.cellNumber() == maxPos.cellNumber()) {
		return JustLoad;
	}

	if (prevMinCellNumber == prevMaxCellNumber &&
		prevMinCellNumber + 1 == minPos.cellNumber() &&
		minPos.cellNumber() == maxPos.cellNumber()) {
		return LoadNextCell;
	}

	if ((type->category() == Type::Category::Array && to<ArrayType>(type)->isByteArray()) ||
		type->category() == Type::Category::TvmCell) {
		return CheckRefs;
	}

	return CheckBits;
}

DecodeFunctionParams::DecodeFunctionParams(StackPusherHelper *pusher) :
		pusher{pusher} {

}

int DecodeFunctionParams::maxBits() {
	// external inbound message
	int maxUsed = 1 + 512 + // signature
				  (pusher->ctx().pragmaHelper().havePubkey()? 1 + 256 : 0) +
				  (pusher->ctx().haveTimeInAbiHeader()? 64 : 0) +
				  (pusher->ctx().pragmaHelper().haveExpire()? 32 : 0) +
				  32; // functionID
	return maxUsed;
}

int DecodeFunctionParams::minBits() {
	return 32; // internal inbound message
}

void DecodeFunctionParams::decodeParameters(const ast_vec<VariableDeclaration> &params) {
	// slice are on stack
	solAssert(pusher->getStack().size() >= 1, "");

	std::unique_ptr<DecodePosition> position;
	switch (pusher->ctx().pragmaHelper().abiVersion()) {
		case 1:
			position = std::make_unique<DecodePositionAbiV1>();
			break;
		case 2: {
			position = std::make_unique<DecodePositionAbiV2>(minBits(), maxBits(), params);
			break;
		}
		default:
			solAssert(false, "");
	}

	pusher->push(0, "; Decode input parameters");
	for (const auto & variable : params) {
		pusher->push(0, "; Decode " + variable->name());
		auto savedStackSize = pusher->getStack().size();
		decodeParameter(variable.get(), position.get());
		pusher->getStack().ensureSize(savedStackSize + 1, "decodeParameter-2");
	}
	pusher->push(-1, "ENDS"); // only ENDS

	solAssert(static_cast<int>(params.size()) <= pusher->getStack().size(), "");
}

void DecodeFunctionParams::loadNextSlice() {
	pusher->push(0, ";; load next cell");
	pusher->push(0, "LDREF");
	pusher->push(0, "ENDS"); // only ENDS
	pusher->push(0, "CTOS");
}

void DecodeFunctionParams::checkBitsAndLoadNextSlice() {
	pusher->pushLines(R"(DUP
SDEMPTY
PUSHCONT {
	LDREF
	ENDS
	CTOS
}
IF
)");
}

void DecodeFunctionParams::checkRefsAndLoadNextSlice() {
	pusher->pushLines(R"(DUP
SREFS
EQINT 1
PUSHCONT {
	LDREF
	ENDS
	CTOS
}
IF
)");
}

void DecodeFunctionParams::checkBitsAndRefsAndLoadNextSlice() {
	// check that bits==0 and ref==1
	pusher->pushLines(R"(DUP
SBITREFS
EQINT 1
SWAP
EQINT 0
AND
PUSHCONT {
	LDREF
	ENDS
	CTOS
}
IF
)");
}

void DecodeFunctionParams::loadNextSliceIfNeed(const DecodePosition::Algo algo, VariableDeclaration const *variable,
											   bool isRefType) {
	switch (algo) {
		case DecodePosition::JustLoad:
			break;
		case DecodePosition::LoadNextCell:
			loadNextSlice();
			break;
		case DecodePosition::CheckBits:
			checkBitsAndLoadNextSlice();
			break;
		case DecodePosition::CheckRefs:
			checkRefsAndLoadNextSlice();
			break;
		case DecodePosition::CheckBitsAndRefs:
			checkBitsAndRefsAndLoadNextSlice();
			break;
		case DecodePosition::Unknown:
			if (isRefType) {
				cast_error(*variable, "Too much refs types");
			}
			checkBitsAndLoadNextSlice();
			break;
	}
}

void DecodeFunctionParams::loadq(const DecodePosition::Algo algo, const std::string &opcodeq, const std::string &opcode) {
	if (algo == DecodePosition::Algo::JustLoad) {
		pusher->push(+1, opcode);
	} else {
		pusher->push(+1, opcodeq);
		pusher->startContinuation();
		loadNextSlice();
		pusher->push(0, opcode);
		pusher->endContinuation();
		pusher->push(0, "IFNOT");
	}
}

void DecodeFunctionParams::decodeParameter(VariableDeclaration const *variable, DecodePosition *position) {
	auto type = getType(variable);
	const Type::Category category = variable->type()->category();
	if (to<TvmCellType>(type)) {
		pusher->push(0, ";; decode TvmCell");
		loadNextSliceIfNeed(position->updateStateAndGetLoadAlgo(type), variable, true);
		pusher->push(+1, "LDREF");
	} else if (auto structType = to<StructType>(type)) {
		ASTString const& structName = structType->structDefinition().name();
		pusher->push(0, ";; decode struct " + structName + " " + variable->name());
		ast_vec<VariableDeclaration> const& members = structType->structDefinition().members();
		for (const ASTPointer<VariableDeclaration> &m : members) {
			pusher->push(0, ";; decode " + structName + "." + m->name());
			decodeParameter(m.get(), position);
		}
		pusher->push(0, ";; build struct " + structName + " ss:" + toString(pusher->getStack().size()));
		// members... slice
		const int memberQty = members.size();
		pusher->blockSwap(memberQty, 1); // slice members...
		pusher->tuple(memberQty); // slice struct
		pusher->push(0, "SWAP"); // ... struct slice
	} else if (category == Type::Category::Address || category == Type::Category::Contract) {
		DecodePosition::Algo algo = position->updateStateAndGetLoadAlgo(type);
		loadq(algo, "LDMSGADDRQ", "LDMSGADDR");
	} else if (isIntegralType(type)) {
		TypeInfo ti{type};
		solAssert(ti.isNumeric, "");
		DecodePosition::Algo algo = position->updateStateAndGetLoadAlgo(type);
		loadq(algo,
			  (ti.isSigned ? "LDIQ " : "LDUQ ") + toString(ti.numBits),
			  (ti.isSigned ? "LDI " : "LDU ") + toString(ti.numBits));
	} else if (auto arrayType = to<ArrayType>(type)) {
		if (arrayType->isByteArray()) {
			loadNextSliceIfNeed(position->updateStateAndGetLoadAlgo(type), variable, true);
			pusher->push(+1, "LDREF");
		} else {
			loadNextSliceIfNeed(position->updateStateAndGetLoadAlgo(type), variable, false);
			pusher->loadArray();
		}
	} else if (to<MappingType>(type)) {
		DecodePosition::Algo algo = position->updateStateAndGetLoadAlgo(type);
		loadq(algo, "LDDICTQ", "LDDICT");
	} else {
		cast_error(*variable, "Unsupported parameter type for decoding: " + type->toString());
	}
}


EncodePosition::EncodePosition(int bits, const std::vector<Type const *> &_types) :
		restSliceBits{TvmConst::CellBitLength - bits},
		restFef{4},
		qtyOfCreatedBuilders{0} {

	for (Type const * t : _types) {
		init(t);
	}

	for (int i = 0; i < static_cast<int>(types.size()); ++i) {
		Type const* t = types[i];
		if (isRefType(t)) {
			lastRefType = i;
		}
	}

	for (int i = 0; i < static_cast<int>(types.size()); ++i) {
		isNeedNewCell.push_back(updateState(i));
	}
}

bool EncodePosition::needNewCell(Type const *type) {
	solAssert(*type == *types[currentIndex], "");
	return isNeedNewCell[currentIndex++];
}

bool EncodePosition::updateState(int i) {
	ABITypeSize size(types[i]);
	solAssert(0 <= size.maxRefs && size.maxRefs <= 1, "");

	restSliceBits -= size.maxBits;
	restFef -= size.maxRefs;
	solAssert(restFef >= 0, "");

	if (i == lastRefType && restFef == 0 && i + 1 == static_cast<int>(types.size())) {
		return false;
	}

	if (restSliceBits < 0 || restFef == 0) {
		restSliceBits =  TvmConst::CellBitLength - size.maxBits;
		restFef = 4 - size.maxRefs;
		++qtyOfCreatedBuilders;
		return true;
	}
	return false;
}

void EncodePosition::init(Type const* t) {
	if (t->category() == Type::Category::Struct) {
		auto members = to<StructType>(t)->structDefinition().members();
		for (const auto &m : members) {
			init(m->type());
		}
	} else {
		types.push_back(t);
	}
}

int EncodePosition::countOfCreatedBuilders() const {
	return qtyOfCreatedBuilders;
}

void EncodeFunctionParams::createMsgBodyAndAppendToBuilder2(const ast_vec<Expression const> &arguments,
															const ReasonOfOutboundMessage reason,
															const CallableDeclaration* funcDef,
															int builderSize) {
	const int saveStackSize = pusher->getStack().size();
	const ast_vec<VariableDeclaration> &parameters = funcDef->parameters();
	solAssert(parameters.size() == arguments.size(), "");
	createMsgBodyAndAppendToBuilder(
			[&](size_t idx) {
				pusher->push(0, ";; " + parameters[idx]->name());
				TVMExpressionCompiler{*pusher}.compileNewExpr(arguments[idx].get());
			},
			reason,
			funcDef,
			false,
			builderSize
	);
			solAssert(saveStackSize == pusher->getStack().size(), "");
}

void EncodeFunctionParams::createDefaultConstructorMessage(const int bitSizeBuilder)
{
	std::vector<ASTPointer<VariableDeclaration>> vect;
	uint32_t funcID = calculateFunctionID("constructor", vect, &vect);
	funcID &= 0x7FFFFFFFu;
	std::stringstream ss;
	ss << "x" << std::hex << std::setfill('0') << std::setw(8) << funcID;

	if (bitSizeBuilder < (1023 - 32 - 1)) {
		pusher->stzeroes(1);
		pusher->push(0, "STSLICECONST " + ss.str());
	} else {
		pusher->stones(1);
		pusher->push(+1, "NEWC");
		pusher->push(0, "STSLICECONST " + ss.str());
		pusher->push(-1, "STBREFR");
	}
}

uint32_t EncodeFunctionParams::calculateFunctionID(const std::string name, const std::vector<ASTPointer<VariableDeclaration>> inputs,
												   const std::vector<ASTPointer<VariableDeclaration>> * outputs) {
	std::stringstream ss;
	ss << name << "(";
	bool comma = false;
	if (pusher->ctx().pragmaHelper().abiVersion() == 1) {
		ss << "time";
		comma = true;
	}
	for (const auto& input : inputs) {
		std::string typestr = getTypeString(input->type(), *input);
		solAssert(!typestr.empty(), "Wrong type in remote function params.");
		if (comma)
			ss << ",";
		ss << typestr;
		comma = true;
	}
	ss << ")";
	comma = false;
	if (outputs) {
		ss << "(";
		for (const auto& output : *outputs) {
			std::string typestr = getTypeString(output->type(), *output);
			solAssert(!typestr.empty(), "Wrong type in remote function params.");
			if (comma)
				ss << ",";
			ss << typestr;
			comma = true;
		}
		ss << ")";
	}
	if (pusher->ctx().pragmaHelper().abiVersion() == 2)
		ss << "v2";
	else
		ss << "v1";

	std::string str = ss.str();
	bytes hash = picosha2::hash256(bytes(
			str.begin(),
			str.end()
	));
	uint32_t funcID = 0;
	for (size_t i = 0; i < 4; i++) {
		funcID <<= 8u;
		funcID += hash[i];
	}

	return funcID;
}


std::pair<uint32_t, bool> EncodeFunctionParams::calculateFunctionID(const CallableDeclaration *declaration) {
	auto functionDefinition = to<FunctionDefinition>(declaration);
	if (functionDefinition != nullptr && functionDefinition->functionID() != 0) {
		return std::make_pair(functionDefinition->functionID(), true);
	}

	std::string name;
	if (functionDefinition != nullptr && functionDefinition->isConstructor())
		name = "constructor";
	else
		name = declaration->name();

	uint32_t id = calculateFunctionID(name, declaration->parameters(), declaration->returnParameterList() ?
								      &declaration->returnParameters() : nullptr);
	return std::make_pair(id, false);									
}

void EncodeFunctionParams::createMsgBodyAndAppendToBuilder(const std::function<void(size_t)> &pushParam,
														   const ReasonOfOutboundMessage &reason,
														   const CallableDeclaration * funcDef,
														   bool encodeReturnParam,
														   const int bitSizeBuilder) {

	const ast_vec<VariableDeclaration> &parameters =
			encodeReturnParam? funcDef->returnParameters() : funcDef->parameters();
	std::vector<Type const*> types = getParams(parameters).first;

	std::unique_ptr<EncodePosition> position = std::make_unique<EncodePosition>(bitSizeBuilder + 32, types);
	const bool doAppend = position->countOfCreatedBuilders() == 0;
	if (doAppend) {
		pusher->stzeroes(1);
	} else {
		pusher->stones(1);
		position = std::make_unique<EncodePosition>(32, types);
		pusher->push(+1, "NEWC");
	}

	createMsgBody(pushParam, reason, funcDef, encodeReturnParam, *position);

	if (!doAppend) {
		pusher->push(-1, "STBREFR");
	}
}

void EncodeFunctionParams::createMsgBody(const std::function<void (size_t)> &pushParam,
										 const ReasonOfOutboundMessage &reason,
										 const CallableDeclaration *funcDef,
										 bool encodeReturnParam,
										 EncodePosition &position)
{
	const ast_vec<VariableDeclaration> &parameters =
			encodeReturnParam? funcDef->returnParameters() : funcDef->parameters();
	std::vector<Type const*> types;
	std::vector<ASTNode const*> nodes;
	std::tie(types, nodes) = getParams(parameters);

	uint32_t funcID;
	bool isManuallyOverridden;
	std::tie(funcID, isManuallyOverridden) = calculateFunctionID(funcDef);
	switch (reason) {
		case ReasonOfOutboundMessage::FunctionReturnExternal:
			funcID |= 0x80000000;
			break;
		case ReasonOfOutboundMessage::EmitEventExternal:
		case ReasonOfOutboundMessage::RemoteCallInternal:
			if (!isManuallyOverridden) {
				funcID &= 0x7FFFFFFFu;
			}
			break;
	}
	std::stringstream ss;
	ss << "x" << std::hex << std::setfill('0') << std::setw(8) << funcID;
	pusher->push(0, "STSLICECONST " + ss.str());
	encodeParameters(types, nodes, pushParam, position);
}

void
EncodeFunctionParams::encodeParameters(const std::vector<Type const *> &types, const std::vector<ASTNode const *> &nodes,
									const std::function<void(size_t)> &pushParam,
									EncodePosition &position) {
	// builder must be situated on top stack
	solAssert(types.size() == nodes.size(), "");
	for (size_t idx = 0; idx < types.size(); idx++) {
		auto type = types[idx];
		encodeParameter(type, position, [&](){pushParam(idx);}, nodes[idx]);
	}
	for (int idx = 0; idx < position.countOfCreatedBuilders(); idx++) {
		pusher->push(-1, "STBREFR");
	}
}

std::string EncodeFunctionParams::getTypeString(Type const * type, const ASTNode &node) {
	if (auto structType = to<StructType>(type)) {
		std::string ret = "(";
		for (size_t i = 0; i < structType->structDefinition().members().size(); i++) {
			if (i != 0) ret += ",";
			ret += getTypeString(structType->structDefinition().members()[i]->type(), node);
		}
		ret += ")";
		return ret;
	} else if (auto arrayType = to<ArrayType>(type)) {
		if (!arrayType->isByteArray())
			return getTypeString(arrayType->baseType(), node) + "[]";
	} else if (auto mapping = to<MappingType>(type)) {
		return "map(" + getTypeString(mapping->keyType(), node) + "," +
			   getTypeString(mapping->valueType(), node) + ")";
	}

	return TVMABI::getParamTypeString(type, node);
}

void EncodeFunctionParams::encodeParameter(Type const *type, EncodePosition &position,
										const std::function<void()> &pushParam, ASTNode const *node) {
	// stack: builder...
	if (auto structType = to<StructType>(type)) {
		pushParam(); // builder... struct
		encodeStruct(structType, node, position); // stack: builder...
	} else {
		if (position.needNewCell(type)) {
			pusher->push(+1, "NEWC");
		}

		if (isIntegralType(type) || isAddressOrContractType(type)) {
			pushParam();
			pusher->push(-1, storeIntegralOrAddress(type, true));
		} else if (auto arrayType = to<ArrayType>(type)) {
			if (arrayType->isByteArray()) {
				pushParam();
				pusher->push(-1, "STREFR");
			} else {
				pushParam();
				// builder array
				pusher->push(-1 + 2, "UNPAIR"); // builder size dict
				pusher->exchange(0, 2); // dict size builder
				pusher->push(-1, "STU 32"); // dict builder
				pusher->push(-1, "STDICT"); // builder
			}
		} else if (to<TvmCellType>(type)) {
			pushParam();
			pusher->push(-1, "STREFR");
		} else if (to<MappingType>(type)) {
			pushParam();
			pusher->push(0, "SWAP");
			pusher->push(-1, "STDICT");
		} else {
			cast_error(*node, "Unsupported type for encoding: " + type->toString());
		}
	}
}

void EncodeFunctionParams::encodeStruct(const StructType* structType, ASTNode const* node, EncodePosition& position) {
	// builder... builder struct
	const int saveStackSize0 = pusher->getStack().size() - 2;
	ast_vec<VariableDeclaration> const& members = structType->structDefinition().members();
	const int memberQty = members.size();
	pusher->untuple(memberQty); // builder... builder values...
	pusher->blockSwap(1, memberQty); // builder... values... builder
	for (int i = 0; i < memberQty; ++i) {
		encodeParameter(members[i]->type(), position, [&]() {
			const int index = pusher->getStack().size() - saveStackSize0 - 1 - i;
			pusher->pushS(index);
		}, node);
	}

	// builder... values... builder...
	const int builderQty = pusher->getStack().size() - saveStackSize0 - memberQty;
	pusher->dropUnder(builderQty, memberQty);
}
