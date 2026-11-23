#include "SR/Common/IL/Verifier.hpp"

#include <algorithm>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "SR/Common/IL/Analysis.hpp"

namespace SR::IL {
namespace {

struct DefSite
{
    BlockId block = InvalidBlockId;
    std::uint32_t instruction = 0;
};

class Verifier
{
public:
    Verifier(const Module& module, std::vector<Diagnostic>* issues, VerificationOptions options)
        : m_module(module)
        , m_issues(issues)
        , m_options(options)
    {
    }

    [[nodiscard]] Status Run()
    {
        if (m_module.types.size() == 0)
            Fail("SR-IL-001", "module has no types");

        if (m_module.entryFunction != InvalidFunctionId &&
            m_module.entryFunction >= m_module.functions.size())
        {
            Fail("SR-IL-002", "entry function id is out of range");
        }

        for (const FunctionId id : m_callStack)
            (void)id;

        for (std::size_t i = 0; i < m_module.functions.size(); ++i)
            VerifyFunction(m_module.functions[i]);

        if (m_options.checkCalls)
            VerifyCallGraph();

        if (m_first)
            return std::unexpected(*m_first);
        return {};
    }

private:
    void Fail(std::string code, std::string message, std::uint32_t pc = 0)
    {
        Diagnostic diagnostic = Diagnostic::Error(std::move(code), std::move(message), pc);
        if (m_issues != nullptr)
            m_issues->push_back(diagnostic);
        if (!m_first)
            m_first = std::move(diagnostic);
    }

    [[nodiscard]] bool ValidType(TypeId id) const
    {
        return id != InvalidType && m_module.types.contains(id);
    }

    [[nodiscard]] bool ValidValue(const Function& function, ValueId id) const
    {
        return id != InvalidValue && id < function.values.size();
    }

    [[nodiscard]] bool ValidBlock(const Function& function, BlockId id) const
    {
        return id != InvalidBlockId && id < function.blocks.size();
    }

    void VerifyFunction(const Function& function)
    {
        if (function.blocks.empty())
        {
            Fail("SR-IL-010", "function '" + function.name + "' has no blocks");
            return;
        }

        if (!ValidBlock(function, function.entry))
            Fail("SR-IL-011", "function '" + function.name + "' has an invalid entry block");

        if (!ValidType(function.returnType))
            Fail("SR-IL-012", "function '" + function.name + "' has an invalid return type");

        for (ValueId i = 0; i < function.values.size(); ++i)
        {
            const Value& value = function.values[i];
            if (!ValidType(value.type))
            {
                Fail("SR-IL-013", "value %" + std::to_string(i) + " has an invalid type");
                continue;
            }

            switch (value.kind)
            {
            case ValueKind::Constant:
                if (value.index >= m_module.constants.size())
                    Fail("SR-IL-014", "value %" + std::to_string(i) + " refers to a missing constant");
                break;
            case ValueKind::Parameter:
                if (value.index >= function.paramValues.size())
                    Fail("SR-IL-015", "value %" + std::to_string(i) + " refers to a missing parameter");
                break;
            case ValueKind::Resource:
                if (value.index >= m_module.resources.size())
                    Fail("SR-IL-016", "value %" + std::to_string(i) + " refers to a missing resource");
                break;
            case ValueKind::Instruction:
                if (!ValidBlock(function, value.block))
                    Fail("SR-IL-017", "value %" + std::to_string(i) + " has an invalid defining block");
                break;
            case ValueKind::Undef:
                break;
            }
        }

        for (std::size_t i = 0; i < function.paramValues.size(); ++i)
        {
            const ValueId id = function.paramValues[i];
            if (!ValidValue(function, id))
            {
                Fail("SR-IL-018", "parameter value is out of range");
                continue;
            }
            const Value& value = function.values[id];
            if (value.kind != ValueKind::Parameter || value.index != i)
                Fail("SR-IL-019", "parameter value does not match its declaration");
            if (i < function.params.size() && function.params[i] != value.type)
                Fail("SR-IL-020", "parameter type does not match its declaration");
        }

        const std::size_t blockCount = function.blocks.size();
        std::vector<std::vector<BlockId>> predecessors(blockCount);
        std::vector<std::vector<BlockId>> successors(blockCount);
        for (BlockId block = 0; block < blockCount; ++block)
        {
            successors[block] = SuccessorsOf(function.blocks[block].terminator);
            for (const BlockId successor : successors[block])
            {
                if (successor < blockCount)
                    predecessors[successor].push_back(block);
            }
        }

        std::vector<DefSite> defSites(function.values.size());
        std::vector<bool> defined(function.values.size(), false);

        for (BlockId block = 0; block < blockCount; ++block)
        {
            const Block& body = function.blocks[block];
            bool sawNonPhi = false;
            for (std::uint32_t index = 0; index < body.body.size(); ++index)
            {
                const Instruction& instruction = body.body[index];
                VerifyInstruction(function, instruction, block, index, predecessors[block], defSites, defined, sawNonPhi);
            }

            VerifyTerminator(function, body.terminator, block, predecessors, successors);
        }

        for (ValueId i = 0; i < function.values.size(); ++i)
        {
            if (function.values[i].kind == ValueKind::Instruction && !defined[i])
                Fail("SR-IL-021", "value %" + std::to_string(i) + " has no definition");
        }

        if (m_options.checkDominance)
            VerifyDominance(function, defSites);
    }

    void VerifyInstruction(const Function& function,
                           const Instruction& instruction,
                           BlockId block,
                           std::uint32_t index,
                           const std::vector<BlockId>& predecessors,
                           std::vector<DefSite>& defSites,
                           std::vector<bool>& defined,
                           bool& sawNonPhi)
    {
        const auto pc = instruction.sourcePc;

        if (instruction.op == Op::Phi)
        {
            if (sawNonPhi)
                Fail("SR-IL-030", "phi instruction is not at the start of its block", pc);

            if (instruction.incoming.size() != predecessors.size())
                Fail("SR-IL-031", "phi has " + std::to_string(instruction.incoming.size()) +
                                       " incoming values for " + std::to_string(predecessors.size()) +
                                       " predecessors",
                     pc);

            std::vector<BlockId> incomingBlocks;
            for (const auto& incoming : instruction.incoming)
            {
                if (!ValidValue(function, incoming.value))
                    Fail("SR-IL-032", "phi incoming value is out of range", pc);
                if (std::find(predecessors.begin(), predecessors.end(), incoming.block) == predecessors.end())
                    Fail("SR-IL-033", "phi incoming block is not a predecessor", pc);
                incomingBlocks.push_back(incoming.block);
            }

            std::sort(incomingBlocks.begin(), incomingBlocks.end());
            if (incomingBlocks.size() == predecessors.size() &&
                !std::equal(incomingBlocks.begin(), incomingBlocks.end(), predecessors.begin()))
            {
                Fail("SR-IL-034", "phi incoming blocks do not match the predecessor set", pc);
            }
        }
        else
        {
            sawNonPhi = true;
            if (!instruction.incoming.empty())
                Fail("SR-IL-035", "non-phi instruction carries phi incoming values", pc);
        }

        for (const ValueId operand : instruction.operands)
        {
            if (!ValidValue(function, operand))
                Fail("SR-IL-036", "operand value is out of range", pc);
        }

        for (const ValueId result : instruction.results)
        {
            if (!ValidValue(function, result))
            {
                Fail("SR-IL-037", "result value is out of range", pc);
                continue;
            }
            if (defined[result])
                Fail("SR-IL-038", "value %" + std::to_string(result) + " is defined more than once", pc);
            defined[result] = true;
            defSites[result] = DefSite{block, index};

            const Value& value = function.values[result];
            if (value.type == InvalidType || !m_module.types.contains(value.type))
                Fail("SR-IL-039", "instruction result has an invalid type", pc);
            if (m_module.types.Get(value.type).kind == TypeKind::Void)
                Fail("SR-IL-040", "instruction result has void type", pc);
        }

        VerifyArity(function, instruction);
        VerifyMemoryTypes(function, instruction, block);

        if (instruction.op == Op::Intrinsic && m_options.checkIntrinsics)
        {
            const auto id = static_cast<IntrinsicId>(instruction.subop);
            if (id > IntrinsicId::TestBufferLoad)
            {
                Fail("SR-IL-041", "intrinsic id is out of range", pc);
            }
            else
            {
                const IntrinsicSpec& spec = GetIntrinsicSpec(id);
                const std::size_t operands = instruction.operands.size();
                const std::size_t results = instruction.results.size();
                if (operands < spec.minOperands || operands > spec.maxOperands)
                    Fail("SR-IL-042", std::string("intrinsic ") + std::string(spec.name) + " has wrong operand count", pc);
                if (results < spec.minResults || results > spec.maxResults)
                    Fail("SR-IL-043", std::string("intrinsic ") + std::string(spec.name) + " has wrong result count", pc);
            }

            if (instruction.payload != NoPayload && instruction.payload > m_module.payloads.size())
                Fail("SR-IL-044", "intrinsic payload id is out of range", pc);
        }

        if (instruction.op == Op::Call)
        {
            if (instruction.callee == InvalidFunctionId || instruction.callee >= m_module.functions.size())
                Fail("SR-IL-045", "call target is not a function", pc);
        }
        else if (instruction.callee != InvalidFunctionId)
        {
            Fail("SR-IL-046", "non-call instruction has a callee", pc);
        }

        if (instruction.op == Op::Kill && index + 1 != function.blocks[block].body.size())
            Fail("SR-IL-047", "discard is not the last instruction in its block", pc);
    }

    void VerifyMemoryTypes(const Function& function, const Instruction& instruction, BlockId block)
    {
        const auto pc = instruction.sourcePc;
        const auto typeOfValue = [&](ValueId value) -> const Type* {
            if (!ValidValue(function, value))
                return nullptr;
            return &m_module.types.Get(function.values[value].type);
        };

        if (instruction.op == Op::Alloca)
        {
            if (block != function.entry)
                Fail("SR-IL-052", "alloca is not in the entry block", pc);
            if (!instruction.results.empty())
            {
                const Type* pointer = typeOfValue(instruction.results[0]);
                if (pointer == nullptr || pointer->kind != TypeKind::Pointer)
                    Fail("SR-IL-053", "alloca result is not a pointer", pc);
            }
            return;
        }

        if (instruction.op == Op::Load)
        {
            if (instruction.operands.empty() || instruction.results.empty())
                return;
            const Type* pointer = typeOfValue(instruction.operands[0]);
            const Type* result = typeOfValue(instruction.results[0]);
            if (pointer == nullptr || pointer->kind != TypeKind::Pointer)
            {
                Fail("SR-IL-054", "load operand is not a pointer", pc);
                return;
            }
            if (result == nullptr || result->kind == TypeKind::Void || result->kind == TypeKind::Pointer)
            {
                Fail("SR-IL-055", "load result is not a scalar value", pc);
                return;
            }
            if (pointer->element != function.values[instruction.results[0]].type)
                Fail("SR-IL-056", "load result type does not match the pointer", pc);
            return;
        }

        if (instruction.op == Op::Store)
        {
            if (instruction.operands.size() < 2)
                return;
            const Type* pointer = typeOfValue(instruction.operands[0]);
            const Type* value = typeOfValue(instruction.operands[1]);
            if (pointer == nullptr || pointer->kind != TypeKind::Pointer)
            {
                Fail("SR-IL-057", "store destination is not a pointer", pc);
                return;
            }
            if (value == nullptr || value->kind == TypeKind::Void || value->kind == TypeKind::Pointer)
            {
                Fail("SR-IL-058", "store value is not a scalar value", pc);
                return;
            }
            if (pointer->element != function.values[instruction.operands[1]].type)
                Fail("SR-IL-059", "store value type does not match the pointer", pc);
        }
    }

    void VerifyArity(const Function& function, const Instruction& instruction)
    {
        const auto pc = instruction.sourcePc;
        const std::size_t operands = instruction.operands.size();
        const std::size_t results = instruction.results.size();
        const auto expect = [&](std::size_t minOperands, std::size_t maxOperands, std::size_t minResults, std::size_t maxResults) {
            if (operands < minOperands || operands > maxOperands)
                Fail("SR-IL-050", std::string("wrong operand count for ") + std::string(ToString(instruction.op)), pc);
            if (results < minResults || results > maxResults)
                Fail("SR-IL-051", std::string("wrong result count for ") + std::string(ToString(instruction.op)), pc);
        };

        switch (instruction.op)
        {
        case Op::Unary:
        case Op::Convert:
        case Op::Bitcast:
            expect(1, 1, 1, 1);
            break;
        case Op::Binary:
        case Op::Compare:
            expect(2, 2, 1, 1);
            break;
        case Op::Ternary:
        case Op::Select:
        case Op::Insert:
            expect(3, 3, 1, 1);
            break;
        case Op::Extract:
            expect(2, 3, 1, 1);
            break;
        case Op::Construct:
            expect(1, 64, 1, 1);
            break;
        case Op::Shuffle:
            expect(2, 64, 1, 1);
            break;
        case Op::Alloca:
            expect(0, 2, 1, 1);
            break;
        case Op::Load:
            expect(1, 2, 1, 1);
            break;
        case Op::Store:
            expect(2, 3, 0, 0);
            break;
        case Op::Atomic:
            expect(2, 5, 0, 2);
            break;
        case Op::Phi:
            expect(0, 0, 1, 1);
            break;
        case Op::Call:
            expect(0, 64, 0, 8);
            break;
        case Op::Kill:
            expect(0, 0, 0, 0);
            break;
        case Op::Intrinsic:
            // Intrinsic arity comes from its spec.
            break;
        case Op::Nop:
            expect(0, 2, 0, 0);
            break;
        }

        (void)function;
    }

    void VerifyTerminator(const Function& function,
                          const Terminator& terminator,
                          BlockId block,
                          const std::vector<std::vector<BlockId>>& predecessors,
                          const std::vector<std::vector<BlockId>>& successors)
    {
        const auto pc = terminator.sourcePc;
        const auto checkTarget = [&](BlockId target) {
            if (!ValidBlock(function, target))
                Fail("SR-IL-060", "terminator targets a block outside the function", pc);
        };

        switch (terminator.kind)
        {
        case TerminatorKind::Br:
            checkTarget(terminator.target);
            break;
        case TerminatorKind::CondBr:
            checkTarget(terminator.trueTarget);
            checkTarget(terminator.falseTarget);
            if (!ValidValue(function, terminator.condition))
                Fail("SR-IL-061", "conditional branch has an invalid condition", pc);
            else if (m_module.types.Get(function.values[terminator.condition].type).kind != TypeKind::Bool)
                Fail("SR-IL-062", "conditional branch condition is not bool", pc);
            break;
        case TerminatorKind::Switch:
            checkTarget(terminator.defaultTarget);
            if (!ValidValue(function, terminator.selector))
                Fail("SR-IL-063", "switch has an invalid selector", pc);
            for (const auto& entry : terminator.cases)
                checkTarget(entry.target);
            break;
        case TerminatorKind::IndirectBr:
            for (const BlockId target : terminator.targets)
                checkTarget(target);
            if (terminator.targets.empty())
                Fail("SR-IL-064", "indirect branch has no targets", pc);
            break;
        case TerminatorKind::Ret:
            if (terminator.hasReturnValue)
            {
                if (!ValidValue(function, terminator.returnValue))
                    Fail("SR-IL-065", "return value is invalid", pc);
                else if (function.values[terminator.returnValue].type != function.returnType)
                    Fail("SR-IL-066", "return value type does not match the function", pc);
            }
            else if (m_module.types.Get(function.returnType).kind != TypeKind::Void)
            {
                Fail("SR-IL-067", "missing return value", pc);
            }
            break;
        case TerminatorKind::Unreachable:
            break;
        }

        if (successors[block].empty() && terminator.kind != TerminatorKind::Ret &&
            terminator.kind != TerminatorKind::Unreachable)
        {
            Fail("SR-IL-068", "block has no successors but is not terminated by ret or unreachable", pc);
        }

        (void)predecessors;
    }

    void VerifyDominance(const Function& function, const std::vector<DefSite>& defSites)
    {
        const FunctionAnalysis analysis = AnalyzeFunction(function);

        const auto checkUse = [&](ValueId value, BlockId useBlock, std::uint32_t useIndex, std::uint32_t pc) {
            if (!ValidValue(function, value))
                return;
            const Value& definition = function.values[value];
            if (definition.kind != ValueKind::Instruction)
                return;

            const DefSite site = defSites[value];
            if (site.block == InvalidBlockId)
                return;

            if (site.block == useBlock)
            {
                if (site.instruction >= useIndex)
                    Fail("SR-IL-070", "value %" + std::to_string(value) + " is used before its definition", pc);
                return;
            }

            if (!analysis.Dominates(site.block, useBlock))
                Fail("SR-IL-071", "value %" + std::to_string(value) + " does not dominate its use", pc);
        };

        for (BlockId block = 0; block < function.blocks.size(); ++block)
        {
            const Block& body = function.blocks[block];
            for (std::uint32_t index = 0; index < body.body.size(); ++index)
            {
                const Instruction& instruction = body.body[index];
                for (const ValueId operand : instruction.operands)
                    checkUse(operand, block, index, instruction.sourcePc);

                for (const auto& incoming : instruction.incoming)
                {
                    if (!ValidValue(function, incoming.value))
                        continue;
                    const Value& definition = function.values[incoming.value];
                    if (definition.kind != ValueKind::Instruction)
                        continue;
                    const DefSite site = defSites[incoming.value];
                    if (site.block == InvalidBlockId)
                        continue;
                    if (!analysis.Dominates(site.block, incoming.block))
                        Fail("SR-IL-072", "phi incoming value does not dominate its predecessor", instruction.sourcePc);
                }
            }

            const Terminator& terminator = body.terminator;
            if (terminator.kind == TerminatorKind::CondBr)
                checkUse(terminator.condition, block, static_cast<std::uint32_t>(body.body.size()), terminator.sourcePc);
            if (terminator.kind == TerminatorKind::Switch)
                checkUse(terminator.selector, block, static_cast<std::uint32_t>(body.body.size()), terminator.sourcePc);
            if (terminator.kind == TerminatorKind::Ret && terminator.hasReturnValue)
                checkUse(terminator.returnValue, block, static_cast<std::uint32_t>(body.body.size()), terminator.sourcePc);
        }
    }

    void VerifyCallGraph()
    {
        const std::size_t count = m_module.functions.size();
        std::vector<std::vector<FunctionId>> edges(count);
        for (std::size_t i = 0; i < count; ++i)
        {
            for (const auto& block : m_module.functions[i].blocks)
            {
                for (const auto& instruction : block.body)
                {
                    if (instruction.op == Op::Call && instruction.callee != InvalidFunctionId &&
                        instruction.callee < count)
                    {
                        edges[i].push_back(instruction.callee);
                    }
                }
            }
        }

        std::vector<std::uint8_t> state(count, 0); // 0 = new, 1 = visiting, 2 = done
        const auto visit = [&](auto&& self, FunctionId id) -> bool {
            if (state[id] == 1)
                return false;
            if (state[id] == 2)
                return true;
            state[id] = 1;
            for (const FunctionId callee : edges[id])
            {
                if (!self(self, callee))
                    return false;
            }
            state[id] = 2;
            return true;
        };

        for (std::size_t i = 0; i < count; ++i)
        {
            if (!visit(visit, static_cast<FunctionId>(i)))
            {
                Fail("SR-IL-080", "call graph contains a cycle");
                return;
            }
        }
    }

    const Module& m_module;
    std::vector<Diagnostic>* m_issues;
    VerificationOptions m_options;
    std::optional<Diagnostic> m_first;
    std::vector<FunctionId> m_callStack;
};

} // namespace

bool IsTerminatorless(const Block& block) noexcept
{
    return block.terminator.kind == TerminatorKind::Unreachable;
}

Status VerifyModule(const Module& module, std::vector<Diagnostic>* issues, VerificationOptions options)
{
    Verifier verifier(module, issues, options);
    return verifier.Run();
}

} // namespace SR::IL
