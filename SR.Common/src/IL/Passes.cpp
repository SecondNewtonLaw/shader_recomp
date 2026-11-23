#include "SR/Common/IL/Passes.hpp"

#include <algorithm>
#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "SR/Common/IL/Analysis.hpp"
#include "SR/Common/IL/Builder.hpp"

namespace SR::IL {
namespace {

bool IsPromotable(const Function& function, ValueId alloca, std::set<BlockId>& defBlocks)
{
    for (BlockId block = 0; block < function.blocks.size(); ++block)
    {
        for (const Instruction& instruction : function.blocks[block].body)
        {
            for (std::size_t i = 0; i < instruction.operands.size(); ++i)
            {
                if (instruction.operands[i] != alloca)
                    continue;

                if (instruction.op == Op::Load && i == 0)
                    continue;
                if (instruction.op == Op::Store && i == 0)
                {
                    defBlocks.insert(block);
                    continue;
                }
                return false;
            }

            for (const auto& incoming : instruction.incoming)
            {
                if (incoming.value == alloca)
                    return false;
            }
        }

        const Terminator& terminator = function.blocks[block].terminator;
        if (terminator.condition == alloca || terminator.selector == alloca || terminator.returnValue == alloca)
            return false;
    }

    return true;
}

ValueId Resolve(const std::vector<ValueId>& replacement, ValueId value)
{
    for (std::size_t i = 0; i < replacement.size(); ++i)
    {
        if (value >= replacement.size() || replacement[value] == InvalidValue || replacement[value] == value)
            return value;
        value = replacement[value];
    }

    return value;
}

void RewriteUses(Function& function, const std::vector<ValueId>& replacement)
{
    for (Block& block : function.blocks)
    {
        for (Instruction& instruction : block.body)
        {
            for (ValueId& operand : instruction.operands)
                operand = Resolve(replacement, operand);
            for (auto& incoming : instruction.incoming)
                incoming.value = Resolve(replacement, incoming.value);
        }

        Terminator& terminator = block.terminator;
        if (terminator.condition != InvalidValue)
            terminator.condition = Resolve(replacement, terminator.condition);
        if (terminator.selector != InvalidValue)
            terminator.selector = Resolve(replacement, terminator.selector);
        if (terminator.hasReturnValue)
            terminator.returnValue = Resolve(replacement, terminator.returnValue);
    }
}

void MarkUndef(Function& function, ValueId value)
{
    if (value >= function.values.size())
        return;
    Value& definition = function.values[value];
    definition.kind = ValueKind::Undef;
    definition.index = 0;
    definition.block = InvalidBlockId;
}

Instruction* FindPhi(Function& function, BlockId block, ValueId phi)
{
    for (Instruction& instruction : function.blocks[block].body)
    {
        if (instruction.op == Op::Phi && !instruction.results.empty() && instruction.results[0] == phi)
            return &instruction;
    }
    return nullptr;
}

Status PromoteFunction(Module& module, Function& function)
{
    RecomputeEdges(function);
    FunctionAnalysis analysis = AnalyzeFunction(function);

    struct Promoted
    {
        ValueId alloca = InvalidValue;
        TypeId type = InvalidType;
        bool uniform = false;
        std::set<BlockId> defBlocks;
        std::map<BlockId, ValueId> phis;
        ValueId undef = InvalidValue;
    };

    std::vector<Promoted> promoted;
    for (const Instruction& instruction : function.blocks[function.entry].body)
    {
        if (instruction.op != Op::Alloca)
            continue;

        Promoted candidate;
        candidate.alloca = instruction.results[0];
        candidate.uniform = function.values[candidate.alloca].uniform;

        const Type& pointer = module.types.Get(function.values[candidate.alloca].type);
        if (pointer.kind != TypeKind::Pointer)
            continue;
        candidate.type = pointer.element;

        if (!IsPromotable(function, candidate.alloca, candidate.defBlocks))
            continue;

        promoted.push_back(std::move(candidate));
    }

    if (promoted.empty())
        return {};

    FunctionBuilder builder(module, function);
    std::vector<ValueId> replacement(function.values.size(), InvalidValue);

    for (Promoted& item : promoted)
    {
        std::set<BlockId> work = item.defBlocks;
        while (!work.empty())
        {
            const BlockId block = *work.begin();
            work.erase(work.begin());

            for (const BlockId frontier : analysis.frontier[block])
            {
                if (item.phis.contains(frontier))
                    continue;

                std::vector<PhiIncoming> incoming;
                incoming.reserve(function.blocks[frontier].preds.size());
                for (const BlockId predecessor : function.blocks[frontier].preds)
                    incoming.push_back(PhiIncoming{InvalidValue, predecessor});

                const ValueId phi = builder.Phi(frontier, item.type, std::move(incoming), item.uniform);
                item.phis.emplace(frontier, phi);

                if (!item.defBlocks.contains(frontier))
                    work.insert(frontier);
            }
        }
    }

    std::vector<std::vector<ValueId>> stacks(promoted.size());

    const auto currentValue = [&](std::size_t index) -> ValueId {
        Promoted& item = promoted[index];
        if (!stacks[index].empty())
            return stacks[index].back();
        if (item.undef == InvalidValue)
            item.undef = builder.Undef(item.type, item.uniform);
        return item.undef;
    };

    const auto rename = [&](auto&& self, BlockId block) -> void {
        std::vector<std::size_t> pushed;

        for (std::size_t i = 0; i < promoted.size(); ++i)
        {
            const auto it = promoted[i].phis.find(block);
            if (it == promoted[i].phis.end())
                continue;
            stacks[i].push_back(it->second);
            pushed.push_back(i);
        }

        Block& body = function.blocks[block];
        for (const Instruction& instruction : body.body)
        {
            if (instruction.op == Op::Load && !instruction.operands.empty() && !instruction.results.empty())
            {
                for (std::size_t i = 0; i < promoted.size(); ++i)
                {
                    if (instruction.operands[0] != promoted[i].alloca)
                        continue;
                    replacement[instruction.results[0]] = currentValue(i);
                    break;
                }
            }
            else if (instruction.op == Op::Store && instruction.operands.size() >= 2)
            {
                for (std::size_t i = 0; i < promoted.size(); ++i)
                {
                    if (instruction.operands[0] != promoted[i].alloca)
                        continue;
                    stacks[i].push_back(instruction.operands[1]);
                    pushed.push_back(i);
                    break;
                }
            }
        }

        for (const BlockId successor : body.succs)
        {
            for (std::size_t i = 0; i < promoted.size(); ++i)
            {
                const auto phiIt = promoted[i].phis.find(successor);
                if (phiIt == promoted[i].phis.end())
                    continue;

                Instruction* phi = FindPhi(function, successor, phiIt->second);
                if (phi == nullptr)
                    continue;

                for (auto& incoming : phi->incoming)
                {
                    if (incoming.block == block)
                        incoming.value = currentValue(i);
                }
            }
        }

        for (const BlockId child : analysis.children[block])
            self(self, child);

        for (const std::size_t index : pushed)
            stacks[index].pop_back();
    };

    rename(rename, function.entry);

    RewriteUses(function, replacement);

    for (const BlockId block : analysis.rpo)
    {
        for (Promoted& item : promoted)
        {
            const auto phiIt = item.phis.find(block);
            if (phiIt == item.phis.end())
                continue;

            Instruction* phi = FindPhi(function, block, phiIt->second);
            if (phi == nullptr)
                continue;

            for (auto& incoming : phi->incoming)
            {
                if (incoming.value == InvalidValue)
                {
                    if (item.undef == InvalidValue)
                        item.undef = builder.Undef(item.type, item.uniform);
                    incoming.value = item.undef;
                }
            }
        }
    }

    std::set<ValueId> promotedAllocas;
    for (const Promoted& item : promoted)
        promotedAllocas.insert(item.alloca);

    for (Block& block : function.blocks)
    {
        std::vector<Instruction> body;
        body.reserve(block.body.size());

        for (Instruction& instruction : block.body)
        {
            if (instruction.op == Op::Alloca && !instruction.results.empty() &&
                promotedAllocas.contains(instruction.results[0]))
            {
                MarkUndef(function, instruction.results[0]);
                continue;
            }

            if ((instruction.op == Op::Load || instruction.op == Op::Store) && !instruction.operands.empty() &&
                promotedAllocas.contains(instruction.operands[0]))
            {
                if (instruction.op == Op::Load && !instruction.results.empty())
                    MarkUndef(function, instruction.results[0]);
                continue;
            }

            body.push_back(std::move(instruction));
        }

        block.body = std::move(body);
    }

    RecomputeEdges(function);
    return {};
}

void RemoveUnreachable(Function& function)
{
    RecomputeEdges(function);
    const FunctionAnalysis analysis = AnalyzeFunction(function);
    if (std::all_of(analysis.reachable.begin(), analysis.reachable.end(), [](bool value) { return value; }))
        return;

    std::vector<BlockId> remap(function.blocks.size(), InvalidBlockId);
    std::vector<Block> blocks;
    blocks.reserve(function.blocks.size());

    for (Value& value : function.values)
    {
        if (value.kind == ValueKind::Instruction && value.block < analysis.reachable.size() &&
            !analysis.reachable[value.block])
        {
            MarkUndef(function, static_cast<ValueId>(&value - function.values.data()));
        }
    }

    for (BlockId old = 0; old < function.blocks.size(); ++old)
    {
        if (!analysis.reachable[old])
            continue;
        remap[old] = static_cast<BlockId>(blocks.size());
        blocks.push_back(std::move(function.blocks[old]));
    }

    const auto mapBlock = [&remap](BlockId block) { return block < remap.size() ? remap[block] : InvalidBlockId; };

    for (Block& block : blocks)
    {
        Terminator& terminator = block.terminator;
        if (terminator.target != InvalidBlockId)
            terminator.target = mapBlock(terminator.target);
        if (terminator.trueTarget != InvalidBlockId)
            terminator.trueTarget = mapBlock(terminator.trueTarget);
        if (terminator.falseTarget != InvalidBlockId)
            terminator.falseTarget = mapBlock(terminator.falseTarget);
        if (terminator.defaultTarget != InvalidBlockId)
            terminator.defaultTarget = mapBlock(terminator.defaultTarget);
        for (auto& entry : terminator.cases)
            entry.target = mapBlock(entry.target);
        for (BlockId& target : terminator.targets)
            target = mapBlock(target);

        for (Instruction& instruction : block.body)
        {
            std::vector<PhiIncoming> incoming;
            incoming.reserve(instruction.incoming.size());
            for (const auto& entry : instruction.incoming)
            {
                const BlockId mapped = mapBlock(entry.block);
                if (mapped != InvalidBlockId)
                    incoming.push_back(PhiIncoming{entry.value, mapped});
            }
            instruction.incoming = std::move(incoming);
        }
    }

    for (Value& value : function.values)
    {
        if (value.kind == ValueKind::Instruction)
            value.block = mapBlock(value.block);
    }

    function.blocks = std::move(blocks);
    function.entry = mapBlock(function.entry);
    RecomputeEdges(function);
}

void EliminateDeadCode(Function& function)
{
    bool changed = true;
    while (changed)
    {
        changed = false;
        std::vector<bool> used(function.values.size(), false);

        for (const Block& block : function.blocks)
        {
            for (const Instruction& instruction : block.body)
            {
                for (const ValueId operand : instruction.operands)
                {
                    if (operand < used.size())
                        used[operand] = true;
                }
                for (const auto& incoming : instruction.incoming)
                {
                    if (incoming.value < used.size())
                        used[incoming.value] = true;
                }
            }

            if (block.terminator.condition != InvalidValue && block.terminator.condition < used.size())
                used[block.terminator.condition] = true;
            if (block.terminator.selector != InvalidValue && block.terminator.selector < used.size())
                used[block.terminator.selector] = true;
            if (block.terminator.hasReturnValue && block.terminator.returnValue < used.size())
                used[block.terminator.returnValue] = true;
        }

        for (Block& block : function.blocks)
        {
            std::vector<Instruction> body;
            body.reserve(block.body.size());

            for (Instruction& instruction : block.body)
            {
                bool remove = false;
                if (instruction.op == Op::Binary || instruction.op == Op::Unary || instruction.op == Op::Compare ||
                    instruction.op == Op::Select || instruction.op == Op::Convert || instruction.op == Op::Bitcast ||
                    instruction.op == Op::Phi || instruction.op == Op::Nop)
                {
                    remove = true;
                    for (const ValueId result : instruction.results)
                    {
                        if (result < used.size() && used[result])
                        {
                            remove = false;
                            break;
                        }
                    }
                }

                if (remove)
                {
                    for (const ValueId result : instruction.results)
                        MarkUndef(function, result);
                    changed = true;
                    continue;
                }

                body.push_back(std::move(instruction));
            }

            block.body = std::move(body);
        }
    }
}

} // namespace

Status AnalyzeCfg(Module& module)
{
    for (Function& function : module.functions)
    {
        if (function.blocks.empty())
            return std::unexpected(Diagnostic::Error("SR-IL-CFA-001", "function has no blocks"));
        if (function.entry == InvalidBlockId || function.entry >= function.blocks.size())
            return std::unexpected(Diagnostic::Error("SR-IL-CFA-002", "function has an invalid entry block"));

        RecomputeEdges(function);
        const FunctionAnalysis analysis = AnalyzeFunction(function);
        if (!analysis.reachable[function.entry])
            return std::unexpected(Diagnostic::Error("SR-IL-CFA-003", "entry block is not reachable"));

        for (BlockId block = 0; block < function.blocks.size(); ++block)
        {
            if (!analysis.reachable[block])
                continue;
            for (const BlockId successor : function.blocks[block].succs)
            {
                if (successor >= function.blocks.size())
                    return std::unexpected(Diagnostic::Error("SR-IL-CFA-004", "branch target is out of range"));
            }
        }
    }

    return {};
}

Status BuildSsa(Module& module, SsaOptions options)
{
    if (!options.promoteAllocas)
        return {};

    for (Function& function : module.functions)
    {
        if (function.blocks.empty())
            continue;
        const auto status = PromoteFunction(module, function);
        if (!status)
            return status;
    }

    return {};
}

Status Optimize(Module& module, OptimizeOptions options)
{
    for (Function& function : module.functions)
    {
        if (function.blocks.empty())
            continue;

        if (options.removeUnreachable)
            RemoveUnreachable(function);

        if (options.eliminateDeadCode)
            EliminateDeadCode(function);
    }

    (void)options.foldConstants;
    return {};
}

Status RunPipeline(Module& module, PipelineOptions options)
{
    if (options.analyzeCfg)
    {
        const auto status = AnalyzeCfg(module);
        if (!status)
            return status;
    }

    if (const auto status = BuildSsa(module, options.ssa); !status)
        return status;

    return Optimize(module, options.optimize);
}

} // namespace SR::IL
