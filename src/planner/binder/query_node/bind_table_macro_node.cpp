#include <iostream>
#include "duckdb/catalog/catalog.hpp"
#include "duckdb/catalog/catalog_entry/macro_catalog_entry.hpp"
#include "duckdb/parser/expression/function_expression.hpp"
#include "duckdb/parser/expression/subquery_expression.hpp"
#include "duckdb/parser/parsed_expression_iterator.hpp"
#include "duckdb/planner/expression_binder.hpp"
#include "duckdb/common/string_util.hpp"

#include "duckdb/common/limits.hpp"
#include "duckdb/common/string_util.hpp"
#include "duckdb/execution/expression_executor.hpp"
#include "duckdb/main/config.hpp"
#include "duckdb/parser/expression/columnref_expression.hpp"
#include "duckdb/parser/expression/comparison_expression.hpp"
#include "duckdb/parser/expression/constant_expression.hpp"
#include "duckdb/parser/expression/subquery_expression.hpp"
#include "duckdb/parser/expression/function_expression.hpp"
#include "duckdb/parser/query_node/select_node.hpp"
#include "duckdb/parser/tableref/joinref.hpp"

#include "duckdb/catalog/catalog_entry/scalar_function_catalog_entry.hpp"
#include "duckdb/planner/binder.hpp"
#include "duckdb/parser/expression/function_expression.hpp"

namespace duckdb {

unique_ptr<QueryNode> Binder::BindTableMacro(FunctionExpression &function, MacroCatalogEntry *macro_func, idx_t depth) {

	auto node = macro_func->function->query_node->Copy();

	auto &macro_def = *macro_func->function;

	// validate the arguments and separate positional and default arguments
	vector<unique_ptr<ParsedExpression>> positionals;
	unordered_map<string, unique_ptr<ParsedExpression>> defaults;
	string error = MacroFunction::ValidateArguments(*macro_func, function, positionals, defaults);
	if (!error.empty()) {
		// cannot use error below as binder rnot in scope
		// return BindResult(binder. FormatError(*expr->get(), error));
		throw BinderException(FormatError(function, error));
	}

	// create a MacroBinding to bind this macro's parameters to its arguments
	vector<LogicalType> types;
	vector<string> names;
	// positional parameters
	for (idx_t i = 0; i < macro_def.parameters.size(); i++) {
		types.emplace_back(LogicalType::SQLNULL);
		auto &param = (ColumnRefExpression &)*macro_def.parameters[i];
		names.push_back(param.GetColumnName());
	}
	// default parameters
	for (auto it = macro_def.default_parameters.begin(); it != macro_def.default_parameters.end(); it++) {
		types.emplace_back(LogicalType::SQLNULL);
		names.push_back(it->first);
		// now push the defaults into the positionals
		positionals.push_back(move(defaults[it->first]));
	}
	auto new_macro_binding = make_unique<MacroBinding>(types, names, macro_func->name);
	new_macro_binding->arguments = move(positionals);

	// We need an EXpressionBinder So that we can call ExpressionBinder::ReplaceMacroParametersRecursive()
	auto eb = ExpressionBinder(*this, this->context);

	eb.macro_binding = new_macro_binding.get();

	/* Does it all goes throu every expression in a selectstmt  */
	ParsedExpressionIterator::EnumerateQueryNodeChildren(
	    *node, [&](unique_ptr<ParsedExpression> &child) { eb.ReplaceMacroParametersRecursive(child); });

	/* from clause
	switch ( select_node.from_table->type ) {

	    case TableReferenceType::EXPRESSION_LIST:
	    case TableReferenceType::SUBQUERY:
	            ParsedExpressionIterator::EnumerateTableRefChildren(
	               *select_node.from_table, [&](unique_ptr<ParsedExpression> &child) {
	eb.ReplaceMacroParametersRecursive(child); }); break;

	    default:
	       break;
	  }
	  */

	return node;
}

} // namespace duckdb
