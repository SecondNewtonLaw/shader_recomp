#include "SR/Common/IL/Analysis.hpp"

#include <algorithm>
#include <cstddef>
#include <map>
#include <set>

namespace SR::IL {

std::vector<BlockId> SuccessorsOf(const Terminator& terminator)
{
    std::vector<BlockId> successors;

    switch (terminator.kind)
    {
    case TerminatorKind::Br:
        if (terminator.target != InvalidBlockId)
            successors.push_back(terminator.target);
        break;
    case TerminatorKind::CondBr:
        if (terminator.trueTarget != InvalidBlockId)
            successors.push_back(terminator.trueTarget);
        if (terminator.falseTarget != InvalidBlockId)
            successors.push_back(terminator.falseTarget);
        break;
    case TerminatorKind::Switch:
        for (const auto& entry : terminator.cases)
        {
            if (entry.target != InvalidBlockId)
                successors.push_back(entry.target);
        }
        if (terminator.defaultTarget != InvalidBlockId)
            successors.push_back(terminator.defaultTarget);
        break;
    case TerminatorKind::IndirectBr:
        successors = terminator.targets;
        break;
    case TerminatorKind::Ret:
    case TerminatorKind::Unreachable:
        break;
    }

    std::sort(successors.begin(), successors.end());
    successors.erase(std::unique(successors.begin(), successors.end()), successors.end());
    return successors;
}

void RecomputeEdges(Function& function)
{
    for (auto& block : function.blocks)
    {
        block.preds.clear();
        block.succs.clear();
    }

    for (BlockId id = 0; id < function.blocks.size(); ++id)
    {
        const auto successors = SuccessorsOf(function.blocks[id].terminator);
        function.blocks[id].succs = successors;
        for (const BlockId successor : successors)
        {
            if (successor < function.blocks.size())
                function.blocks[successor].preds.push_back(id);
        }
    }
}

FunctionAnalysis AnalyzeFunction(const Function& function)
{
    FunctionAnalysis analysis;
    const std::size_t blockCount = function.blocks.size();
    analysis.idom.assign(blockCount, InvalidBlockId);
    analysis.children.assign(blockCount, {});
    analysis.frontier.assign(blockCount, {});
    analysis.order.assign(blockCount, NoBlock);
    analysis.reachable.assign(blockCount, false);

    if (function.entry == InvalidBlockId || function.entry >= blockCount)
        return analysis;

    // Iterative postorder DFS over successors.
    std::vector<BlockId> postorder;
    {
        std::vector<std::pair<BlockId, std::size_t>> stack;
        std::vector<bool> visited(blockCount, false);
        stack.emplace_back(function.entry, 0);
        visited[function.entry] = true;

        while (!stack.empty())
        {
            auto& [block, next] = stack.back();
            const auto successors = SuccessorsOf(function.blocks[block].terminator);
            if (next < successors.size())
            {
                const BlockId successor = successors[next++];
                if (successor >= blockCount || visited[successor])
                    continue;
                visited[successor] = true;
                stack.emplace_back(successor, 0);
            }
            else
            {
                postorder.push_back(block);
                stack.pop_back();
            }
        }
    }

    analysis.rpo.assign(postorder.rbegin(), postorder.rend());
    for (std::size_t i = 0; i < analysis.rpo.size(); ++i)
    {
        analysis.order[analysis.rpo[i]] = i;
        analysis.reachable[analysis.rpo[i]] = true;
    }

    // Cooper-Harvey-Kennedy immediate dominators.
    std::vector<std::vector<BlockId>> predecessors(blockCount);
    for (BlockId block = 0; block < blockCount; ++block)
    {
        for (const BlockId successor : SuccessorsOf(function.blocks[block].terminator))
        {
            if (successor < blockCount)
                predecessors[successor].push_back(block);
        }
    }

    analysis.idom[function.entry] = function.entry;
    bool changed = true;
    while (changed)
    {
        changed = false;
        for (const BlockId block : analysis.rpo)
        {
            if (block == function.entry)
                continue;

            BlockId newIdom = InvalidBlockId;
            for (const BlockId predecessor : predecessors[block])
            {
                if (predecessor >= blockCount || !analysis.reachable[predecessor])
                    continue;
                if (analysis.idom[predecessor] == InvalidBlockId)
                    continue;
                if (newIdom == InvalidBlockId)
                {
                    newIdom = predecessor;
                    continue;
                }

                BlockId first = predecessor;
                BlockId second = newIdom;
                while (first != second)
                {
                    while (analysis.order[first] > analysis.order[second])
                        first = analysis.idom[first];
                    while (analysis.order[second] > analysis.order[first])
                        second = analysis.idom[second];
                }
                newIdom = first;
            }

            if (analysis.idom[block] != newIdom)
            {
                analysis.idom[block] = newIdom;
                changed = true;
            }
        }
    }

    for (const BlockId block : analysis.rpo)
    {
        if (block == function.entry)
            continue;
        const BlockId parent = analysis.idom[block];
        if (parent != InvalidBlockId)
            analysis.children[parent].push_back(block);
    }

    // Dominance frontiers.
    for (const BlockId block : analysis.rpo)
    {
        if (predecessors[block].size() < 2)
            continue;

        for (const BlockId predecessor : predecessors[block])
        {
            BlockId runner = predecessor;
            while (runner != InvalidBlockId && runner != analysis.idom[block])
            {
                analysis.frontier[runner].push_back(block);
                if (runner == analysis.idom[runner])
                    break;
                runner = analysis.idom[runner];
            }
        }
    }

    for (auto& list : analysis.frontier)
    {
        std::sort(list.begin(), list.end());
        list.erase(std::unique(list.begin(), list.end()), list.end());
    }

    // Natural loops: a back edge is an edge whose target dominates its source.
    std::map<BlockId, std::set<BlockId>> loopBlocks;
    for (const BlockId block : analysis.rpo)
    {
        for (const BlockId successor : SuccessorsOf(function.blocks[block].terminator))
        {
            if (!analysis.Dominates(successor, block))
                continue;

            std::set<BlockId> members{successor};
            std::vector<BlockId> work{block};
            while (!work.empty())
            {
                const BlockId current = work.back();
                work.pop_back();
                if (!members.insert(current).second)
                    continue;
                for (const BlockId predecessor : predecessors[current])
                    work.push_back(predecessor);
            }
            auto& blocks = loopBlocks[successor];
            blocks.insert(members.begin(), members.end());
        }
    }

    for (auto& [header, blocks] : loopBlocks)
    {
        NaturalLoop loop;
        loop.header = header;
        loop.blocks.assign(blocks.begin(), blocks.end());
        analysis.loops.push_back(std::move(loop));
    }

    return analysis;
}

bool FunctionAnalysis::Dominates(BlockId a, BlockId b) const noexcept
{
    if (a >= idom.size() || b >= idom.size())
        return false;
    if (a == b)
        return true;
    if (!reachable[a] || !reachable[b])
        return false;

    BlockId current = b;
    while (current != InvalidBlockId)
    {
        if (current == a)
            return true;
        const BlockId parent = idom[current];
        if (parent == current)
            return false;
        current = parent;
    }

    return false;
}

} // namespace SR::IL
