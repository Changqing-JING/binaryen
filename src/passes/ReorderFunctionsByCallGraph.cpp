#include <cmath>
#include <cstdint>
#include <limits>
#include <list>
#include <map>
#include <numeric>
#include <queue>
#include <tuple>
#include <unordered_map>

#include <cfg/cfg-traversal.h>
#include <ir/branch-hints.h>
#include <ir/element-utils.h>
#include <ir/find_all.h>
#include <ir/utils.h>
#include <pass.h>
#include <support/strongly_connected_components.h>
#include <wasm.h>

namespace wasm {

struct ReorderFunctionsByCallGraph : public Pass {
  using Weight = uint64_t;

  struct Node {
    std::map<Index, Weight> outgoing;
    std::map<Index, Weight> incoming;
    std::list<Index> placement;
    Index first;
    uint64_t size = 0;
    Weight weight = 0;
    Weight initialWeight = 0;
    bool active = true;

    double density() const { return double(weight) / double(size); }
  };

  struct Edge {
    Weight weight;
    Index source;
    Index target;

    bool operator<(const Edge& other) const {
      if (weight != other.weight) {
        return weight < other.weight;
      }
      return std::tie(source, target) > std::tie(other.source, other.target);
    }
  };

  bool requiresNonNullableLocalFixups() override { return false; }

  using CallRates = std::vector<std::map<Index, double>>;

  struct CallGraphSCCs
    : SCCs<std::vector<Index>::const_iterator, CallGraphSCCs> {
    const CallRates& rates;

    CallGraphSCCs(const std::vector<Index>& functions, const CallRates& rates)
      : SCCs(functions.begin(), functions.end()), rates(rates) {}

    void pushChildren(Index source) {
      for (auto [target, rate] : rates[source]) {
        push(target);
      }
    }
  };

  struct BlockContents {
    std::vector<Name> calls;
    std::optional<double> takenProbability;
    std::optional<double> comparisonProbability;
    bool firstSuccessorIsTaken = true;
    bool constantCondition = false;
    std::optional<double> estimatedWeight;
  };

  struct FrequencyScanner
    : CFGWalker<FrequencyScanner, Visitor<FrequencyScanner>, BlockContents> {
    using Base =
      CFGWalker<FrequencyScanner, Visitor<FrequencyScanner>, BlockContents>;
    static constexpr Index noLoop = std::numeric_limits<Index>::max();
    static constexpr long double maxFrequency = 1e100L;
    struct LoopRegion {
      Index begin;
      Index end = 0;
      Index parent;
      std::map<Index, long double> exits;
      long double entryMass = 0;
      long double scale = 1;
      long double multiplier = 1;
      std::vector<Index> members = {};
    };
    std::vector<LoopRegion> loops;
    std::vector<Index> activeLoops;
    std::unordered_map<BasicBlock*, BasicBlock*> normalSuccessors;
    std::vector<Name> referencedFunctions;

    FrequencyScanner() { ignoreBranchesOutsideOfFunc = true; }

    void visitCall(Call* curr) {
      if (currBasicBlock) {
        currBasicBlock->contents.calls.push_back(curr->target);
      }
    }

    void visitRefFunc(RefFunc* curr) {
      referencedFunctions.push_back(curr->func);
    }

    void visitUnreachable(Unreachable*) {
      if (currBasicBlock) {
        currBasicBlock->contents.estimatedWeight = 0;
      }
    }
    void visitThrow(Throw*) {
      if (currBasicBlock) {
        currBasicBlock->contents.estimatedWeight = 1;
      }
    }
    void visitRethrow(Rethrow*) { visitThrow(nullptr); }
    void visitThrowRef(ThrowRef*) { visitThrow(nullptr); }

    static std::optional<double> comparisonProbability(Expression* condition) {
      Expression* operand = nullptr;
      bool likely;
      if (auto* unary = condition->dynCast<Unary>();
          unary && (unary->op == EqZInt32 || unary->op == EqZInt64)) {
        operand = unary->value;
        likely = false;
      } else if (auto* binary = condition->dynCast<Binary>()) {
        if (binary->op == EqFloat32 || binary->op == EqFloat64) {
          return 12.0 / 32;
        }
        if (binary->op == NeFloat32 || binary->op == NeFloat64) {
          return 20.0 / 32;
        }
        auto* constant = binary->right->dynCast<Const>();
        if (!constant ||
            (constant->type != Type::i32 && constant->type != Type::i64)) {
          return std::nullopt;
        }
        auto value = constant->type == Type::i32
                       ? int64_t(constant->value.geti32())
                       : constant->value.geti64();
        switch (binary->op) {
          case EqInt32:
          case EqInt64:
            if (value != 0 && value != -1) {
              return std::nullopt;
            }
            likely = false;
            break;
          case NeInt32:
          case NeInt64:
            if (value != 0 && value != -1) {
              return std::nullopt;
            }
            likely = true;
            break;
          case LtSInt32:
          case LtSInt64:
            if (value != 0 && value != 1) {
              return std::nullopt;
            }
            likely = false;
            break;
          case GtSInt32:
          case GtSInt64:
            if (value != 0 && value != -1) {
              return std::nullopt;
            }
            likely = true;
            break;
          default:
            return std::nullopt;
        }
        operand = binary->left;
      } else {
        return std::nullopt;
      }
      if (auto* binary = operand->dynCast<Binary>();
          binary && (binary->op == AndInt32 || binary->op == AndInt64)) {
        if (auto* constant = binary->right->dynCast<Const>()) {
          auto mask = constant->type == Type::i32
                        ? uint64_t(uint32_t(constant->value.geti32()))
                        : uint64_t(constant->value.geti64());
          if (mask && !(mask & (mask - 1))) {
            return std::nullopt;
          }
        }
      }
      return likely ? 20.0 / 32 : 12.0 / 32;
    }

    void noteCondition(Expression* branch, Expression* condition) {
      if (!currBasicBlock) {
        return;
      }
      if (auto* constant = condition->dynCast<Const>()) {
        currBasicBlock->contents.constantCondition = true;
        currBasicBlock->contents.takenProbability =
          constant->value.geti32() != 0 ? 1.0 : 0.0;
      } else if (auto hint = BranchHints::get(branch, getFunction())) {
        currBasicBlock->contents.takenProbability =
          *hint ? 2000.0 / 2001 : 1.0 / 2001;
      } else {
        currBasicBlock->contents.comparisonProbability =
          comparisonProbability(condition);
      }
    }

    static void doStartIfTrue(FrequencyScanner* self, Expression** currp) {
      self->noteCondition(*currp, (*currp)->cast<If>()->condition);
      Base::doStartIfTrue(self, currp);
    }

    static void doEndBranch(FrequencyScanner* self, Expression** currp) {
      if (auto* branch = (*currp)->dynCast<Switch>()) {
        if (auto* constant = branch->condition->dynCast<Const>()) {
          auto selector = uint32_t(constant->value.geti32());
          auto target = selector < branch->targets.size()
                          ? branch->targets[selector]
                          : branch->default_;
          self->branches[target].push_back(self->currBasicBlock);
        } else {
          for (auto target : branch->targets) {
            self->branches[target].push_back(self->currBasicBlock);
          }
          self->branches[branch->default_].push_back(self->currBasicBlock);
        }
        self->startUnreachableBlock();
        return;
      }
      if (auto* branch = (*currp)->dynCast<Break>();
          branch && branch->condition && self->currBasicBlock) {
        self->noteCondition(branch, branch->condition);
        self->currBasicBlock->contents.firstSuccessorIsTaken = false;
      }
      Base::doEndBranch(self, currp);
    }

    static void doEndCall(FrequencyScanner* self, Expression** currp) {
      auto* source = self->currBasicBlock;
      Base::doEndCall(self, currp);
      if (source && source != self->currBasicBlock) {
        self->normalSuccessors[source] = self->currBasicBlock;
      }
    }

    static void doStartLoop(FrequencyScanner* self, Expression** currp) {
      Base::doStartLoop(self, currp);
      auto parent =
        self->activeLoops.empty() ? noLoop : self->activeLoops.back();
      auto index = Index(self->loops.size());
      self->loops.push_back(
        {Index(self->basicBlocks.size() - 1), 0, parent, {}});
      self->activeLoops.push_back(index);
    }

    static void doEndLoop(FrequencyScanner* self, Expression** currp) {
      self->loops[self->activeLoops.back()].end = self->basicBlocks.size();
      self->activeLoops.pop_back();
      Base::doEndLoop(self, currp);
    }

    std::map<Name, double> estimate() {
      std::unordered_map<BasicBlock*, Index> indices;
      auto alive = findLiveBlocks();
      for (Index index = 0; index < basicBlocks.size(); ++index) {
        indices[basicBlocks[index].get()] = index;
      }
      std::vector<Index> loopAtHeader(basicBlocks.size(), noLoop);
      for (Index index = 0; index < loops.size(); ++index) {
        loopAtHeader[loops[index].begin] = index;
      }
      std::vector<Index> probabilityOwner(basicBlocks.size(), noLoop);
      std::vector<Index> visited(basicBlocks.size(), noLoop);
      for (Index regionIndex = 0; regionIndex < loops.size(); ++regionIndex) {
        auto& region = loops[regionIndex];
        std::vector<Index> pending;
        for (auto* predecessor : basicBlocks[region.begin]->in) {
          auto index = indices.at(predecessor);
          if (index >= region.begin && index < region.end &&
              alive.contains(predecessor)) {
            pending.push_back(index);
          }
        }
        while (!pending.empty()) {
          auto index = pending.back();
          pending.pop_back();
          if (visited[index] == regionIndex) {
            continue;
          }
          visited[index] = regionIndex;
          probabilityOwner[index] = regionIndex;
          region.members.push_back(index);
          if (index == region.begin) {
            continue;
          }
          for (auto* predecessor : basicBlocks[index]->in) {
            auto previous = indices.at(predecessor);
            if (previous >= region.begin && previous < region.end &&
                alive.contains(predecessor)) {
              pending.push_back(previous);
            }
          }
        }
      }
      auto isInLoop = [&](Index block, Index region) {
        auto current = probabilityOwner[block];
        while (current != noLoop && current != region) {
          current = loops[current].parent;
        }
        return region != noLoop && current == region;
      };
      std::vector<std::optional<double>> blockWeights(basicBlocks.size());
      std::vector<std::optional<double>> loopWeights(loops.size());
      std::vector<std::vector<std::pair<Index, Index>>> loopExits(loops.size());
      for (Index index = 0; index < basicBlocks.size(); ++index) {
        blockWeights[index] = basicBlocks[index]->contents.estimatedWeight;
      }
      for (Index region = 0; region < loops.size(); ++region) {
        for (auto source : loops[region].members) {
          for (auto* next : basicBlocks[source]->out) {
            auto target = indices.at(next);
            if (!isInLoop(target, region)) {
              loopExits[region].push_back({source, target});
            }
          }
        }
      }
      auto getEdgeWeight = [&](Index source, Index target) {
        auto region = probabilityOwner[target];
        if (region != noLoop && !isInLoop(source, region)) {
          return loopWeights[region];
        }
        return blockWeights[target];
      };
      bool changed;
      do {
        changed = false;
        for (Index region = loops.size(); region > 0; --region) {
          auto index = region - 1;
          if (loopWeights[index] || loopExits[index].empty()) {
            continue;
          }
          std::optional<double> maximum = 0;
          for (auto [source, target] : loopExits[index]) {
            auto weight = getEdgeWeight(source, target);
            if (!weight) {
              maximum = std::nullopt;
              break;
            }
            maximum = std::max(*maximum, *weight);
          }
          if (maximum) {
            loopWeights[index] = std::max(1.0, *maximum);
            changed = true;
          }
        }
        for (Index blockIndex = basicBlocks.size(); blockIndex > 0;
             --blockIndex) {
          auto index = blockIndex - 1;
          auto* block = basicBlocks[index].get();
          if (blockWeights[index] || !alive.contains(block) ||
              block->out.empty()) {
            continue;
          }
          std::optional<double> maximum = 0;
          for (auto* next : block->out) {
            auto weight = getEdgeWeight(index, indices.at(next));
            if (!weight) {
              maximum = std::nullopt;
              break;
            }
            maximum = std::max(*maximum, *weight);
          }
          if (maximum) {
            blockWeights[index] = maximum;
            changed = true;
          }
        }
      } while (changed);
      std::vector<Index> owner(basicBlocks.size(), noLoop);
      std::vector<Index> stack;
      for (Index index = 0; index < basicBlocks.size(); ++index) {
        while (!stack.empty() && loops[stack.back()].end <= index) {
          stack.pop_back();
        }
        if (loopAtHeader[index] != noLoop) {
          stack.push_back(loopAtHeader[index]);
        }
        if (!stack.empty()) {
          owner[index] = stack.back();
        }
      }
      std::vector<std::map<Index, long double>> probabilities(
        basicBlocks.size());
      for (Index index = 0; index < basicBlocks.size(); ++index) {
        auto* block = basicBlocks[index].get();
        if (!alive.contains(block)) {
          continue;
        }
        std::vector<double> weights;
        double total = 0;
        bool foundEstimatedWeight = false;
        for (Index successor = 0; successor < block->out.size(); ++successor) {
          auto* next = block->out[successor];
          auto target = indices.at(next);
          auto estimatedWeight = getEdgeWeight(index, target);
          if (normalSuccessors.contains(block) &&
              normalSuccessors.at(block) != next) {
            estimatedWeight = std::min(estimatedWeight.value_or(1.0), 1.0);
          }
          if (probabilityOwner[index] != noLoop &&
              !isInLoop(target, probabilityOwner[index]) &&
              estimatedWeight != 0) {
            estimatedWeight =
              std::max(1.0, estimatedWeight.value_or(1048575) / 31);
          }
          foundEstimatedWeight |= estimatedWeight.has_value();
          auto weight = estimatedWeight.value_or(1048575);
          if (auto probability = block->contents.takenProbability) {
            bool taken =
              (successor == 0) == block->contents.firstSuccessorIsTaken;
            weight = taken ? *probability : 1 - *probability;
            if (!block->contents.constantCondition && estimatedWeight == 0) {
              weight = 0;
            }
          }
          weights.push_back(weight);
          total += weight;
        }
        if (!block->contents.takenProbability &&
            (!foundEstimatedWeight || total == 0)) {
          total = 0;
          for (Index successor = 0; successor < weights.size(); ++successor) {
            double weight = 1;
            if (auto probability = block->contents.comparisonProbability) {
              bool taken =
                (successor == 0) == block->contents.firstSuccessorIsTaken;
              weight = taken ? *probability : 1 - *probability;
            }
            weights[successor] = weight;
            total += weight;
          }
        }
        if (total == 0 && !weights.empty()) {
          std::fill(weights.begin(), weights.end(), 1);
          total = weights.size();
        }
        if (total == 0) {
          continue;
        }
        for (Index successor = 0; successor < block->out.size(); ++successor) {
          probabilities[index][indices.at(block->out[successor])] +=
            weights[successor] / total;
        }
      }
      auto functionRegion = Index(loops.size());
      loops.push_back({0, Index(basicBlocks.size()), noLoop, {}});
      std::vector<long double> mass(basicBlocks.size(), 0);
      std::vector<long double> localMass(basicBlocks.size(), 0);
      auto distribute = [&](Index regionIndex) {
        auto& region = loops[regionIndex];
        for (Index index = region.begin; index < region.end;) {
          mass[index] = 0;
          auto child = loopAtHeader[index];
          index = child != noLoop && child != regionIndex ? loops[child].end
                                                          : index + 1;
        }
        mass[region.begin] = 1;
        long double backedgeMass = 0;
        auto transfer = [&](Index target, long double flow) {
          if (regionIndex != functionRegion && target == region.begin) {
            backedgeMass += flow;
          } else if (target < region.begin || target >= region.end) {
            region.exits[target] += flow;
          } else {
            mass[target] += flow;
          }
        };
        for (Index index = region.begin; index < region.end;) {
          auto child = loopAtHeader[index];
          if (child != noLoop && child != regionIndex) {
            loops[child].entryMass = mass[index];
            for (auto [target, probability] : loops[child].exits) {
              transfer(target, mass[index] * probability);
            }
            index = loops[child].end;
          } else {
            localMass[index] = mass[index];
            for (auto [target, probability] : probabilities[index]) {
              transfer(target, mass[index] * probability);
            }
            ++index;
          }
        }
        auto exitMass = 1 - backedgeMass;
        region.scale =
          exitMass > 0 ? std::min(maxFrequency, 1 / exitMass) : 4096;
        for (auto& [target, probability] : region.exits) {
          probability *= region.scale;
        }
      };
      for (Index index = functionRegion; index > 0; --index) {
        distribute(index - 1);
      }
      distribute(functionRegion);
      for (Index index = 0; index < functionRegion; ++index) {
        auto& loop = loops[index];
        auto parentMultiplier =
          loop.parent == noLoop ? 1 : loops[loop.parent].multiplier;
        loop.multiplier = std::min(
          maxFrequency, parentMultiplier * loop.entryMass * loop.scale);
      }
      std::map<Name, double> calls;
      for (Index index = 0; index < basicBlocks.size(); ++index) {
        auto multiplier =
          owner[index] == noLoop ? 1 : loops[owner[index]].multiplier;
        auto frequency = std::min(maxFrequency, localMass[index] * multiplier);
        for (auto target : basicBlocks[index]->contents.calls) {
          calls[target] = double(std::min(
            maxFrequency, static_cast<long double>(calls[target]) + frequency));
        }
      }
      return calls;
    }
  };

  void estimateCallGraph(Module* module,
                         const std::unordered_map<Name, Index>& indices,
                         std::vector<Node>& nodes) {
    CallRates rates(nodes.size());
    std::vector<double> roots(nodes.size(), 0);
    auto noteRoot = [&](Name name) {
      auto index = indices.at(name);
      if (nodes[index].active) {
        roots[index] = 1;
      }
    };
    if (module->start.is()) {
      noteRoot(module->start);
    }
    for (auto& export_ : module->exports) {
      if (export_->kind == ExternalKind::Function) {
        noteRoot(*export_->getInternalName());
      }
    }
    ElementUtils::iterAllElementFunctionNames(module, noteRoot);
    for (auto& global : module->globals) {
      if (global->init) {
        for (auto* reference : FindAll<RefFunc>(global->init).list) {
          noteRoot(reference->func);
        }
      }
    }
    for (Index source = 0; source < nodes.size(); ++source) {
      if (!nodes[source].active) {
        continue;
      }
      FrequencyScanner scanner;
      scanner.walkFunctionInModule(module->functions[source].get(), module);
      for (auto [name, rate] : scanner.estimate()) {
        auto target = indices.at(name);
        if (nodes[target].active && rate > 0) {
          rates[source][target] = rate;
        }
      }
      for (auto name : scanner.referencedFunctions) {
        noteRoot(name);
      }
    }
    if (std::none_of(
          roots.begin(), roots.end(), [](double root) { return root > 0; })) {
      for (Index index = 0; index < nodes.size(); ++index) {
        roots[index] = nodes[index].active ? 1 : 0;
      }
    }
    std::vector<Index> functions(nodes.size());
    std::iota(functions.begin(), functions.end(), 0);
    std::vector<std::vector<Index>> components;
    std::vector<Index> componentOf(nodes.size());
    for (auto component : CallGraphSCCs(functions, rates)) {
      std::vector<Index> members;
      for (auto member : component) {
        componentOf[member] = components.size();
        members.push_back(member);
      }
      std::sort(members.begin(), members.end());
      components.push_back(std::move(members));
    }
    constexpr long double maxCount = 1e100L;
    std::vector<long double> entries(roots.begin(), roots.end());
    std::vector<long double> additional(nodes.size(), 0);
    for (auto component = components.rbegin(); component != components.rend();
         ++component) {
      for (auto source : *component) {
        for (auto [target, rate] : rates[source]) {
          if (componentOf[source] == componentOf[target]) {
            additional[target] =
              std::min(maxCount, additional[target] + entries[source] * rate);
          }
        }
      }
      for (auto member : *component) {
        entries[member] =
          std::min(maxCount, entries[member] + additional[member]);
      }
      for (auto source : *component) {
        for (auto [target, rate] : rates[source]) {
          if (componentOf[source] != componentOf[target]) {
            entries[target] =
              std::min(maxCount, entries[target] + entries[source] * rate);
          }
        }
      }
    }
    long double total = 0;
    for (Index source = 0; source < nodes.size(); ++source) {
      for (auto [target, rate] : rates[source]) {
        total += std::min(maxCount, entries[source] * rate);
      }
    }
    auto scale = std::min(1048576.0L,
                          std::numeric_limits<Weight>::max() /
                            (2 * std::max(1.0L, total)));
    for (Index source = 0; source < nodes.size(); ++source) {
      for (auto [target, rate] : rates[source]) {
        auto weight =
          Weight(std::min(maxCount, entries[source] * rate) * scale);
        if (source == target) {
          nodes[source].weight += weight;
        } else if (weight > 0) {
          nodes[source].outgoing[target] = weight;
          nodes[target].incoming[source] = weight;
        }
      }
    }
  }

  void runHfSort(std::vector<Node>& nodes) {
    constexpr uint64_t maxClusterSize = 1024 * 1024;
    constexpr double maxDensityDegradation = 8;
    std::vector<Index> sorted;
    std::vector<Index> leaders(nodes.size());
    for (Index index = 0; index < nodes.size(); ++index) {
      leaders[index] = index;
      if (nodes[index].active) {
        for (auto [source, weight] : nodes[index].incoming) {
          nodes[index].weight += weight;
        }
        nodes[index].initialWeight = nodes[index].weight;
        sorted.push_back(index);
      }
    }
    std::stable_sort(
      sorted.begin(), sorted.end(), [&](Index left, Index right) {
        return nodes[left].density() > nodes[right].density();
      });
    auto getLeader = [&](Index index) {
      while (leaders[index] != index) {
        leaders[index] = leaders[leaders[index]];
        index = leaders[index];
      }
      return index;
    };
    for (auto index : sorted) {
      auto& target = nodes[index];
      if (!target.active || target.incoming.empty()) {
        continue;
      }
      auto best = target.incoming.begin();
      for (auto current = target.incoming.begin();
           current != target.incoming.end();
           ++current) {
        if (current->second > best->second) {
          best = current;
        }
      }
      if (double(best->second) * 10 <= double(target.initialWeight)) {
        continue;
      }
      auto sourceIndex = getLeader(best->first);
      auto& source = nodes[sourceIndex];
      if (sourceIndex == index || source.size + target.size > maxClusterSize) {
        continue;
      }
      auto density = double(source.weight + target.weight) /
                     double(source.size + target.size);
      if (density < source.density() / maxDensityDegradation) {
        continue;
      }
      source.placement.splice(source.placement.end(), target.placement);
      source.size += target.size;
      source.weight += target.weight;
      source.first = std::min(source.first, target.first);
      target.active = false;
      leaders[index] = sourceIndex;
    }
  }

  void run(Module* module) override {
    auto algorithm =
      getArgumentOrDefault("reorder-functions-by-call-graph", "textbook");
    if (algorithm != "textbook" && algorithm != "hfsort" &&
        algorithm != "hfsort-estimated") {
      Fatal() << "expected textbook, hfsort, or hfsort-estimated for "
                 "reorder-functions-by-call-graph";
    }
    bool estimated = algorithm == "hfsort-estimated";
    bool hfsort = algorithm != "textbook";
    std::unordered_map<Name, Index> indices;
    std::vector<Node> nodes(module->functions.size());
    for (Index index = 0; index < module->functions.size(); ++index) {
      indices.emplace(module->functions[index]->name, index);
      nodes[index].placement.push_back(index);
      nodes[index].first = index;
    }

    struct CallScanner : public PostWalker<CallScanner> {
      const std::unordered_map<Name, Index>& indices;
      std::vector<Node>& nodes;
      Index source;

      CallScanner(const std::unordered_map<Name, Index>& indices,
                  std::vector<Node>& nodes,
                  Index source)
        : indices(indices), nodes(nodes), source(source) {}

      void visitCall(Call* curr) {
        auto target = indices.at(curr->target);
        if (target != source && nodes[target].active) {
          ++nodes[source].outgoing[target];
          ++nodes[target].incoming[source];
        }
      }
    };

    for (Index index = 0; index < module->functions.size(); ++index) {
      nodes[index].active = !module->functions[index]->imported();
      if (nodes[index].active && hfsort) {
        nodes[index].size =
          std::max(
            uint64_t(1),
            uint64_t(Measurer::measure(module->functions[index]->body))) *
          4;
      }
    }
    std::priority_queue<Edge> edges;
    if (estimated) {
      estimateCallGraph(module, indices, nodes);
    }
    for (Index index = 0; index < module->functions.size(); ++index) {
      if (nodes[index].active && !estimated) {
        CallScanner(indices, nodes, index).walk(module->functions[index]->body);
        for (auto [target, weight] : nodes[index].outgoing) {
          edges.push({weight, index, target});
        }
      }
    }

    auto addEdge = [&](Index source, Index target, Weight weight) {
      auto total = nodes[source].outgoing[target] += weight;
      nodes[target].incoming[source] = total;
      edges.push({total, source, target});
    };
    auto removeEdge = [&](Index source, Index target) {
      nodes[source].outgoing.erase(target);
      nodes[target].incoming.erase(source);
    };

    if (hfsort) {
      runHfSort(nodes);
    }
    while (!hfsort && !edges.empty()) {
      auto edge = edges.top();
      edges.pop();
      auto& source = nodes[edge.source];
      auto& target = nodes[edge.target];
      if (!source.active || !target.active) {
        continue;
      }
      auto current = source.outgoing.find(edge.target);
      if (current == source.outgoing.end() || current->second != edge.weight) {
        continue;
      }

      removeEdge(edge.source, edge.target);
      removeEdge(edge.target, edge.source);
      for (auto [neighbor, weight] : target.outgoing) {
        nodes[neighbor].incoming.erase(edge.target);
        addEdge(edge.source, neighbor, weight);
      }
      for (auto [neighbor, weight] : target.incoming) {
        nodes[neighbor].outgoing.erase(edge.target);
        addEdge(neighbor, edge.source, weight);
      }
      source.placement.splice(source.placement.end(), target.placement);
      source.first = std::min(source.first, target.first);
      target.outgoing.clear();
      target.incoming.clear();
      target.active = false;
    }

    std::vector<Index> clusters;
    std::vector<std::unique_ptr<Function>> reordered;
    reordered.reserve(module->functions.size());
    for (Index index = 0; index < module->functions.size(); ++index) {
      if (module->functions[index]->imported()) {
        reordered.push_back(std::move(module->functions[index]));
      } else if (nodes[index].active) {
        clusters.push_back(index);
      }
    }
    std::sort(clusters.begin(), clusters.end(), [&](Index left, Index right) {
      if (hfsort && nodes[left].density() != nodes[right].density()) {
        return nodes[left].density() > nodes[right].density();
      }
      return nodes[left].first < nodes[right].first;
    });
    for (auto cluster : clusters) {
      for (auto index : nodes[cluster].placement) {
        reordered.push_back(std::move(module->functions[index]));
      }
    }
    module->functions = std::move(reordered);
  }
};

Pass* createReorderFunctionsByCallGraphPass() {
  return new ReorderFunctionsByCallGraph();
}

} // namespace wasm