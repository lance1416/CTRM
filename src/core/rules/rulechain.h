// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "rules/rule.h"

#include <QString>

#include <memory>
#include <optional>
#include <vector>

namespace ctrm {

// Options of a rule chain. New-chain defaults are fixed (spec 10a.7).
struct ChainOptions
{
    bool portableNames = true;
    bool autoResolveDuplicates = false;
    bool folderExtensions = false;
    bool operator==(const ChainOptions &) const = default;
};

// An ordered list of rules. The chain owns its rules. A copy clones each rule.
class RuleChain
{
public:
    RuleChain();
    RuleChain(const RuleChain &other);
    RuleChain &operator=(const RuleChain &other);
    RuleChain(RuleChain &&) noexcept;
    RuleChain &operator=(RuleChain &&) noexcept;
    ~RuleChain();

    ChainOptions options;

    int size() const;
    Rule *at(int i);
    const Rule *at(int i) const;
    void insert(int index, std::unique_ptr<Rule> rule);
    std::unique_ptr<Rule> take(int index);
    void move(int from, int to);
    void duplicate(int index); // copy goes to index + 1
    void clear();

    struct Error
    {
        int ruleIndex;
        RuleProblem problem;
    };
    std::optional<Error> firstError() const; // first enabled rule with a problem

    // Applies each enabled rule in order. Each rule sees the name split again.
    QString apply(const QString &name, const RuleContext &ctx) const;

private:
    std::vector<std::unique_ptr<Rule>> m_rules;
};

} // namespace ctrm
