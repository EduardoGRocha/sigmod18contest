#ifndef PROJECTIONOPERATOR_H_
#define PROJECTIONOPERATOR_H_

#include "Operator.h"
#include "Relation.h"


class ProjectionOperator final : public Operator{
private:
	// list of columns and relationIds
	std::vector<std::pair<const column_t*,unsigned>> projections;
	// resulting sums
	std::vector<uint64_t> results;
	// number of tuples reached projection, to distinguish sum==0 and NULL because of no tuples
	uint64_t amount = 0;
	uint64_t size;

#if defined(ENABLE_ASMJIT) || defined(ENABLE_LLVMJIT)
	template<class Fn>
	void codegen_impl(Fn &fn, CodegenContext<Fn> &ctx){
#ifndef QUIET
		puts("ProjectionOperator");
#endif
		for(size_t i=0; i<size; ++i){
			auto [column, relid] = projections[i];
			auto val = loadValue(fn, *column, ctx.rowids[relid]);
			ctx.results[i] += val;
		}
		++ctx.amount;
	}

	template<class Fn>
	void codegen_save_impl(Fn &fn, CodegenContext<Fn> &ctx){
#ifndef QUIET
		puts("ProjectionOperator save");
#endif
		for(size_t i=0; i<size; ++i){
			auto &projaddr = std::get<2>(ctx.arguments);
			projaddr[i] = ctx.results[i];
		}
	}
#endif

public:
	ProjectionOperator(
		const std::vector<Relation> &relations,
		const std::vector<unsigned> &bindings,
		const std::vector<Selection> &selections
	){
		for(const auto &s : selections){
			unsigned relid = bindings[s.relationId];
			projections.emplace_back(&relations[relid].getColumn(s.columnId), s.relationId);
		}
		// initialize to 0
		results.resize(selections.size(), 0);

		size = selections.size();
	}

	void execute(Context *ctx) override{
		for(size_t i=0; i<size; ++i){
			auto [column, relid] = projections[i];
			uint64_t val = loadValue(*column, ctx->rowids[relid]);
			results[i] += val;
		}
		++amount;
	}

#ifdef ENABLE_ASMJIT
	void codegen(Fn_asmjit &fn, CodegenContext<Fn_asmjit> &ctx) override { codegen_impl(fn, ctx); }
	void codegen_save(Fn_asmjit &fn, CodegenContext<Fn_asmjit> &ctx){ codegen_save_impl(fn, ctx); }
#endif
#ifdef ENABLE_LLVMJIT
	void codegen(Fn_llvmjit &fn, CodegenContext<Fn_llvmjit> &ctx) override { codegen_impl(fn, ctx); }
	void codegen_save(Fn_llvmjit &fn, CodegenContext<Fn_llvmjit> &ctx){ codegen_save_impl(fn, ctx); }
#endif

	const std::vector<uint64_t> &getResults() const {
		return results;
	}
	uint64_t getAmount() const {
		return amount;
	}
};

#endif