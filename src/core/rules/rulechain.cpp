// SPDX-License-Identifier: GPL-3.0-or-later
#include "rules/rulechain.h"

#include "model/nameparts.h"

#include <utility>

namespace ctrm {

RuleChain::RuleChain() = default;

RuleChain::RuleChain(const RuleChain &other)
    : options(other.options)
{
    m_rules.reserve(other.m_rules.size());
    for (const auto &rule : other.m_rules)
        m_rules.push_back(rule->clone());
}

RuleChain &RuleChain::operator=(const RuleChain &other)
{
    if (this != &other) {
        RuleChain copy(other);
        *this = std::move(copy);
    }
    return *this;
}

RuleChain::RuleChain(RuleChain &&) noexcept = default;
RuleChain &RuleChain::operator=(RuleChain &&) noexcept = default;
RuleChain::~RuleChain() = default;

int RuleChain::size() const
{
    return int(m_rules.size());
}

Rule *RuleChain::at(int i)
{
    Q_ASSERT(i >= 0 && i < size());
    return m_rules[size_t(i)].get();
}

const Rule *RuleChain::at(int i) const
{
    Q_ASSERT(i >= 0 && i < size());
    return m_rules[size_t(i)].get();
}

void RuleChain::insert(int index, std::unique_ptr<Rule> rule)
{
    Q_ASSERT(rule);
    Q_ASSERT(index >= 0 && index <= size());
    m_rules.insert(m_rules.begin() + index, std::move(rule));
}

std::unique_ptr<Rule> RuleChain::take(int index)
{
    Q_ASSERT(index >= 0 && index < size());
    std::unique_ptr<Rule> rule = std::move(m_rules[size_t(index)]);
    m_rules.erase(m_rules.begin() + index);
    return rule;
}

void RuleChain::move(int from, int to)
{
    Q_ASSERT(from >= 0 && from < size());
    Q_ASSERT(to >= 0 && to < size());
    if (from == to)
        return;
    std::unique_ptr<Rule> rule = take(from);
    insert(to, std::move(rule));
}

void RuleChain::duplicate(int index)
{
    Q_ASSERT(index >= 0 && index < size());
    insert(index + 1, at(index)->clone());
}

void RuleChain::clear()
{
    m_rules.clear();
}

std::optional<RuleChain::Error> RuleChain::firstError() const
{
    for (int i = 0; i < size(); ++i) {
        const Rule *rule = at(i);
        if (!rule->enabled)
            continue;
        if (auto problem = rule->problem())
            return Error{i, *problem};
    }
    return std::nullopt;
}

QString RuleChain::apply(const QString &name, const RuleContext &ctx) const
{
    QString current = name;
    for (const auto &rule : m_rules) {
        if (!rule->enabled)
            continue;
        NameParts parts = splitName(current, ctx.isFolder, ctx.folderExtensions);
        rule->apply(parts, ctx);
        current = joinName(parts);
    }
    return current;
}

} // namespace ctrm
