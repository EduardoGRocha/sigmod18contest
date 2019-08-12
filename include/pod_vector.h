#ifndef POD_VECTOR_H_
#define POD_VECTOR_H_

#include <cstdlib>
#include <cassert>

#include "coat/Struct.h"
#include "coat/ControlFlow.h"


template<typename T, size_t initial_size=1024>
class pod_vector {
	//FIXME: way too strict, every relocatable object works
	static_assert(std::is_pod_v<T>, "pod_vector only supports pod types");

#define MEMBERS(x) \
	x(T*, start) \
	x(T*, finish) \
	x(T*, capend)

DECLARE_PRIVATE(MEMBERS)
#undef MEMBERS

public:
	using value_type = T;

	pod_vector(){
		start = (T*)malloc(initial_size * sizeof(T));
		finish = start;
		capend = start + initial_size;
	}
	~pod_vector(){
		free(start);
	}
	pod_vector(const pod_vector&) = delete;
	pod_vector(pod_vector&&) = default;

	void swap(pod_vector &other){
		T *tmp_start=start, *tmp_finish=finish, *tmp_capend=capend;
		start = other.start;
		finish = other.finish;
		capend = other.capend;
		other.start = tmp_start;
		other.finish = tmp_finish;
		other.capend = tmp_capend;
	}

	bool      empty() const noexcept { return start == finish; }
	size_t     size() const noexcept { return finish - start; }
	size_t capacity() const noexcept { return capend - start; }

	void reserve(size_t newsize){
		size_t size = (finish - start) * sizeof(T);
		start = (T*)realloc(start, newsize*sizeof(T));
		finish = (T*)(((char*)start) + size); // ugly
		capend = (T*)(((char*)start) + (newsize*sizeof(T))); // ugly
	}
	void resize(size_t size){
		//FIXME: not meant for growing, but not checked at all
		finish = start + size;
	}

	      T &operator[](size_t index)       noexcept { return start[index]; }
	const T &operator[](size_t index) const noexcept { return start[index]; }

	T *data() noexcept { return start; }

	      T &front()       { return *start; }
	const T &front() const { return *start; }
	      T &back()       { return finish[-1]; }
	const T &back() const { return finish[-1]; }
	      T *begin()        noexcept { return start; }
	const T *begin()  const noexcept { return start; }
	const T *cbegin() const noexcept { return start; }
	      T *end()        noexcept { return finish; }
	const T *end()  const noexcept { return finish; }
	const T *cend() const noexcept { return finish; }

	bool operator==(const pod_vector &other) const noexcept {
		size_t sze = size(), osze = other.size();
		if(sze != osze) return false;
		return memcmp(start, other.start, sze) == 0;
	}
	bool operator!=(const pod_vector &other) const noexcept {
		return !operator==(other);
	}

	inline void push_back(T value){
		if(finish == capend){
			//FIXME: if size==0, then size << 1 is still 0
			size_t size = (capend - start) * sizeof(T);
			start = (T*)realloc(start, size << 1);
			finish = (T*)(((char*)start) + size); // ugly
			capend = (T*)(((char*)start) + (size << 1)); // ugly
		}
		*finish = value;
		++finish;
	}

	void pop_back(){
		// fails when empty
		assert(!empty());
		--finish;
	}

	void clear(){
		finish = start;
	}

	//TODO: proper erase support
	// poor man's erase of a range at the end
	void eraseEnd(T *newEnd){
		finish = newEnd;
	}

	template<typename ...Args>
	void emplace_back(Args &&...args){
		if(finish == capend){
			//FIXME: if size==0, then size << 1 is still 0
			size_t size = (capend - start) * sizeof(T);
			start = (T*)realloc(start, size << 1);
			finish = (T*)(((char*)start) + size); // ugly
			capend = (T*)(((char*)start) + (size << 1)); // ugly
		}
		new(finish) T(std::forward<Args>(args)...);
		++finish;
	}
};


#if defined(ENABLE_ASMJIT) || defined(ENABLE_LLVMJIT)
namespace coat {

template<typename T, size_t I>
struct has_custom_base<pod_vector<T,I>> : std::true_type {};


template<class CC, typename T, size_t I>
struct StructBase<Struct<CC,pod_vector<T,I>>> {
	using PV = pod_vector<T,I>;

	auto begin() const {
		auto &self = static_cast<const Struct<CC,PV>&>(*this);
		return self.template get_value<PV::member_start>();
	}
	auto end() const {
		auto &self = static_cast<const Struct<CC,PV>&>(*this);
		return self.template get_value<PV::member_finish>();
	}

	void push_back(Value<CC,T> &value){
		auto &self = static_cast<Struct<CC,PV>&>(*this);
		//FIXME: accessed each time
		auto vr_finish = self.template get_value<PV::member_finish>();
		auto vr_capend = self.template get_value<PV::member_capend>();
		// grow in size if full
		coat::if_then(self.cc, vr_finish == vr_capend, [&]{
			auto vr_start = self.template get_value<PV::member_start>();
			// get size in bytes
			auto vr_size = coat::distance(self.cc, vr_start, vr_finish);
			// call realloc with increased size
			using realloc_type = T *(*)(T*,size_t); // fix realloc void* type issue, coat has no cast
			auto vr_newstart = coat::FunctionCall(self.cc, (realloc_type)realloc, "realloc", vr_start, vr_size << 1);
			// assign members of vector new values after realloc
			self.template get_reference<PV::member_start>() = vr_newstart;
			//vr_finish = vr_newstart += vr_size; //FIXME: does ptr arithmetic
			vr_finish = vr_newstart.addByteOffset(vr_size);
			//self.template get_reference<PV::member_capend>() = vr_newstart += vr_size; //FIXME: does ptr arithmetic
			self.template get_reference<PV::member_capend>() = vr_newstart.addByteOffset(vr_size);
		});
		*vr_finish = value;
		++vr_finish;
		self.template get_reference<PV::member_finish>() = vr_finish;
	}

	Value<CC,size_t> size() const {
		auto &self = static_cast<const Struct<CC,PV>&>(*this);
		//FIXME: accessed each time
		auto vr_start = self.template get_value<PV::member_start>();
		auto vr_finish = self.template get_value<PV::member_finish>();
		return vr_finish - vr_start;
	}
};

}
#endif

#endif
