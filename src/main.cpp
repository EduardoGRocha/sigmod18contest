#include <cstdio>
#include <vector>

#ifndef DISABLE_OPENMP
#include <omp.h>
#endif

#include "Relation.h"
#include "Query.h"
#include "RelationalOperators.h"


static std::vector<Relation> parseInit(const char *fname){
	std::vector<Relation> relations;

	FILE *fd = fopen(fname, "r");
	if(!fd){
		perror("fopen failed");
		exit(EXIT_FAILURE);
	}
	// line buffer, allocated and reallocated by getline()
	char *line=nullptr;
	size_t len=0;

	ssize_t nread;
	while((nread=getline(&line, &len, fd)) != -1){
		// remove included newline
		line[nread-1] = '\0';
		//Relation rel(line);
		//printf("relation %s; number of tuples: %lu\n", line, rel.getNumberOfTuples());
		relations.emplace_back(line);
		//FIXME: we just assume that relation x is in on line x in init file
		//       otherwise we would need a mapping
	}

	free(line); // allocated by getline()
	fclose(fd);

	return relations;
}

static void precalc(std::vector<Relation> &relations){
#ifndef DISABLE_OPENMP
	// parallelize per column in relation as well
	size_t work = 0;
	for(auto &r : relations){
		work += r.getNumberOfColumns();
		r.stats_init();
	}
#ifndef QUIET
	printf("precalculating %lu indices, %i threads\n", work, omp_get_max_threads());
#endif
	size_t threads = omp_get_max_threads();
	if(work < threads) threads = work; //TODO: in this case one should also disable parallel sort
	#pragma omp parallel for schedule(static,1) num_threads(threads)
	for(size_t i=0; i<work; ++i){
		//HACK: a lot of sequential work to get to the right column/work item
		size_t j = i;
		for(auto &r : relations){
			if(j < r.getNumberOfColumns()){
				r.stats(j);
				break;
			}
			j -= r.getNumberOfColumns();
		}
	}
#else
	// precalculate sequentially
	for(auto &r : relations){
		r.stats_init();
		for(size_t c=0; c<r.getNumberOfColumns(); ++c){
			r.stats(c);
		}
	}
#endif
}


void printResult(uint64_t amount, const uint64_t *results, uint64_t rsize, FILE *fd_out){
	if(amount != 0){
		printf("%lu", results[0]);
		fprintf(fd_out, "%lu", results[0]);
		for(size_t r=1; r<rsize; ++r){
			printf(" %lu", results[r]);
			fprintf(fd_out, " %lu", results[r]);
		}
	}else{
		// no result records, print NULL
		printf("NULL");
		fprintf(fd_out, "NULL");
		for(size_t r=1; r<rsize; ++r){
			printf(" NULL");
			fprintf(fd_out, " NULL");
		}
	}
	printf("\n");
	fprintf(fd_out, "\n");
}
void printResult(ProjectionOperator *proj, FILE *fd_out){
	// extract result from projection operator
	const auto &results = proj->getResults();
	printResult(proj->getAmount(), results.data(), results.size(), fd_out);
}


using executeFunc = void (*)(const Query &q, ScanOperator *scan, ProjectionOperator *proj, FILE *fd_out, void *data, size_t query);

void parseWork(const char *fname, std::vector<Relation> &relations, executeFunc execfn, void *data){
	FILE *fd_out = fopen("output.res", "w");
	if(!fd_out){
		perror("fopen failed");
		exit(EXIT_FAILURE);
	}

	FILE *fd = fopen(fname, "r");
	if(!fd){
		perror("fopen failed");
		exit(EXIT_FAILURE);
	}
	// line buffer, allocated and reallocated by getline()
	char *line=nullptr;
	size_t len=0;

	Query q;
	ssize_t nread;
	size_t query=1;
	while((nread=getline(&line, &len, fd)) != -1){
		if(*line == 'F') continue;
#ifndef QUIET
		printf("%lu: %s", query, line);
#endif
#ifdef MEASURE_TIME
		auto t_start = std::chrono::high_resolution_clock::now();
#endif
		// remove included newline
		line[nread-1] = '\0';
		// parse query string
		q.parse(line);

#ifndef QUIET
		if(q.filters.empty()){
			printf("no filter :: ");
		}else{
			printf("filter(");
			for(const auto &f : q.filters){
				printf("%u,", f.sel.relationId);
			}
			printf(") :: ");
		}
		for(unsigned relid : q.relationIds){
			printf("r%u: %lu; ", relid, relations[relid].getNumberOfTuples());
		}
		printf("\n");
#endif

		q.rewrite(relations);
		auto [scan,proj] = q.constructPipeline(relations);

#ifdef MEASURE_TIME
		auto t_prepare = std::chrono::high_resolution_clock::now();
#endif

		execfn(q, scan, proj, fd_out, data, query);

		// deallocate pipeline
		delete scan;
#ifdef MEASURE_TIME
		auto t_end = std::chrono::high_resolution_clock::now();
#ifndef QUIET
		printf("prepare: %11.2f us\n  query: %11.2f us\n  total: %11.2f us\n",
			std::chrono::duration<double, std::micro>( t_prepare - t_start).count(),
			std::chrono::duration<double, std::micro>( t_end - t_prepare).count(),
			std::chrono::duration<double, std::micro>( t_end - t_start).count()
		);
#endif

		prepare_time += std::chrono::duration<double, std::micro>( t_prepare - t_start).count();
#endif
		// clear query
		q.clear();
		++query;
	}

	free(line); // allocated by getline()
	fclose(fd);

	fclose(fd_out);
}


void tupleByTuple(const Query &q, ScanOperator *scan, ProjectionOperator *proj, FILE *fd_out, void*, size_t){
	// execute non-codegen
	Context ctx(q.relationIds.size());
	scan->execute(&ctx);

	printResult(proj, fd_out);
}


int main(int argc, char *argv[]){
	if(argc < 4){
		puts("./program -[t|a|l] init work");
		return -1;
	}

#ifndef DISABLE_OPENMP
	// setting for OpenMP
	omp_set_nested(true);
	omp_set_max_active_levels(2);
#endif

	auto t_start = std::chrono::high_resolution_clock::now();

	std::vector<Relation> relations = parseInit(argv[2]);
	precalc(relations);

	auto t_init = std::chrono::high_resolution_clock::now();

#if 0
	// init JIT engines
	runtimeAsmjit asmrt;
	LLVMJIT::initTarget();
	LLVMJIT llvmjit;
#endif
	char *p = &argv[1][1];
	while(*p){
		if(*p == 't'){
			parseWork(argv[3], relations, tupleByTuple, nullptr);
#if 0
		}else if(*p == 'a'){
			parseWork(argv[3], relations, codegenAsmjit, &asmrt);
		}else if(*p == 'l'){
			switch(p[1]){
				case '0': llvmjit.setOptLevel(0); break;
				case '1': llvmjit.setOptLevel(1); break;
				case '2': llvmjit.setOptLevel(2); break;
				case '3': llvmjit.setOptLevel(3); break;
				default: break;
			}
			parseWork(argv[3], relations, codegenLLVMjit, &llvmjit);
#endif
		}
		++p;
	}

	auto t_end = std::chrono::high_resolution_clock::now();
	printf(" init: %12.2f us\n work: %12.2f us\ntotal: %12.2f us\n",
		std::chrono::duration<double, std::micro>(t_init - t_start).count(),
		std::chrono::duration<double, std::micro>( t_end - t_init ).count(),
		std::chrono::duration<double, std::micro>( t_end - t_start).count()
	);

	return 0;
}
