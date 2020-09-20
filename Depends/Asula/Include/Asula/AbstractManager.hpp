#pragma once

#include <boost/noncopyable.hpp>
#include <type_traits>
#include <cstdint>
#include <string>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <cassert>

#define DECLEARE_MEMBER_CHECKER(MEMBER)\
template<typename T, typename... Args> struct has_##MEMBER\
{\
private:\
    template<typename U> static auto check(int) -> decltype(std::declval<U>().MEMBER(std::declval<Args>()...), std::true_type());\
    template<typename U> static auto check(...) -> decltype(std::false_type());\
public:\
    static const bool value = std::is_same<decltype(check<T>(0)), std::true_type>::value;\
};

DECLEARE_MEMBER_CHECKER(id)
DECLEARE_MEMBER_CHECKER(code)



#define GET_PTR(m, k) m.count(k) == 0 ? nullptr : m.at(k)

template<typename T>
class AbstractManager : boost::noncopyable
{
    static_assert(!std::is_pointer<T>::value && !std::is_void<T>::value, "Only support basic type, no pointer or void");
    using TPtr = std::shared_ptr<T>;

public:
    virtual ~AbstractManager() {};

public:
    template<typename U = T>
    void add(TPtr t, typename std::enable_if<(has_id<U>::value) && (has_code<U>::value)>::type* = nullptr)
    {
        assert(t != nullptr);
        if (t == nullptr)
        {
            return;
        }

        std::lock_guard<std::mutex> lock(mutex_);
        assert(idToPtrs_.count(t->id()) == 0);
        assert(codeToPtrs_.count(t->code()) == 0);

        idToPtrs_[t->id()] = t;
        codeToPtrs_[t->code()] = t;
        ptrs_.push_back(t);
    }

    template<typename U = T>
    void add(TPtr t, typename std::enable_if<(has_id<U>::value) && (!has_code<U>::value)>::type* = nullptr)
    {
        assert(t != nullptr);
        if (t == nullptr)
        {
            return;
        }

        std::lock_guard<std::mutex> lock(mutex_);
        assert(idToPtrs_.count(t->id()) == 0);

        idToPtrs_[t->id()] = t;
        ptrs_.push_back(t);
    }

    template<typename U = T>
    void add(TPtr t, typename std::enable_if<(!has_id<U>::value) && (has_code<U>::value)>::type* = nullptr)
    {
        assert(t != nullptr);
        if (t == nullptr)
        {
            return;
        }

        std::lock_guard<std::mutex> lock(mutex_);
        const std::string &code = t->code();
        assert(codeToPtrs_.count(code) == 0);

        codeToPtrs_[t->code()] = t;
        ptrs_.push_back(t);
    }

    template<typename U = T>
    void rmv(TPtr t, typename std::enable_if<(has_id<U>::value) && (has_code<U>::value)>::type* = nullptr)
    {
        if (t == nullptr)
        {
            return;
        }
        std::lock_guard<std::mutex> lock(mutex_);
        ptrs_.remove(t);
        idToPtrs_.erase(t->id());
        codeToPtrs_.erase(t->code());
    }

    template<typename U = T>
    void rmv(TPtr t, typename std::enable_if<(has_id<U>::value) && (!has_code<U>::value)>::type* = nullptr)
    {
        if (t == nullptr)
        {
            return;
        }
        std::lock_guard<std::mutex> lock(mutex_);
        ptrs_.remove(t);
        idToPtrs_.erase(t->id());
    }

    template<typename U = T>
    void rmv(TPtr t, typename std::enable_if<(!has_id<U>::value) && (has_code<U>::value)>::type* = nullptr)
    {
        if (t == nullptr)
        {
            return;
        }
        std::lock_guard<std::mutex> lock(mutex_);
        ptrs_.remove(t);
        codeToPtrs_.erase(t->code());
    }

    template<typename U = T>
    TPtr get(int id, typename std::enable_if<has_id<U>::value>::type* = nullptr)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return GET_PTR(idToPtrs_, id);
    }

    template<typename U = T>
    TPtr get(const std::string& code, typename std::enable_if<has_code<U>::value>::type* = nullptr)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return GET_PTR(codeToPtrs_, code);
    }

    const std::list<TPtr>& list()
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return ptrs_;
    }

    bool empty()
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return ptrs_.empty();
    }

    size_t size()
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return ptrs_.size();
    }

    void clear()
    {
        std::lock_guard<std::mutex> lock(mutex_);
        ptrs_.clear();
        idToPtrs_.clear();
        codeToPtrs_.clear();
    }


protected:
    std::mutex mutex_;
    std::unordered_map<int, TPtr> idToPtrs_;
    std::unordered_map<std::string, TPtr> codeToPtrs_;
    std::list<TPtr> ptrs_;
};
