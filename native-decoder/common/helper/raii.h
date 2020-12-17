#include <type_traits>
#include <functional>

namespace common {
/* 元模板，如果是const类型则去除const修饰符 */
template <typename T> struct no_const {
    using type = typename std::conditional<std::is_const<T>::value, typename std::remove_const<T>::type, T>::type;
};
/*
 * RAII方式管理申请和释放资源的类
 * 对象创建时,执行acquire(申请资源)动作(可以为空函数[]{})
 * 对象析构时,执行release(释放资源)动作
 * 禁止对象拷贝和赋值
 */
class RAII {
public:
    using fun_type = std::function<void()>;
    /* release: 析构时执行的函数
     * acquire: 构造函数执行的函数
     * default_com:_commit,默认值,可以通过commit()函数重新设置
     */
    explicit RAII(
        fun_type release, fun_type acquire = [] {}, bool default_com = true) noexcept
        : _commit(default_com), _release(release) {
        acquire();
    }
    /* 对象析构时根据_commit标志执行_release函数 */
    ~RAII() noexcept {
        if (_commit) _release();
    }
    /* 移动构造函数 允许右值赋值 */
    RAII(RAII &&rv) noexcept : _commit(rv._commit), _release(rv._release) { rv._commit = false; };
    /* 禁用拷贝构造函数 */
    RAII(const RAII &) = delete;
    /* 禁用赋值操作符 */
    RAII &operator=(const RAII &) = delete;

    /* 设置_commit标志 */
    RAII &commit(bool c = true) noexcept {
        _commit = c;
        return *this;
    };

private:
    /* 为true时析构函数执行_release */
    bool _commit;

protected:
    /* 析构时执的行函数 */
    std::function<void()> _release;
}; /* RAII */

/* 用于实体资源的raii管理类
 * T为资源类型
 * acquire为申请资源动作，返回资源T
 * release为释放资源动作,释放资源T
 */
template <typename T> class RAIIVar {
public:
    using _Self    = RAIIVar<T>;
    using acq_type = std::function<T()>;
    using rel_type = std::function<void(T &)>;
    explicit RAIIVar(acq_type acquire, rel_type release) noexcept : _resource(acquire()), _release(release) {
        //构造函数中执行申请资源的动作acquire()并初始化resource;
    }
    /* 移动构造函数 */
    RAIIVar(RAIIVar &&rv) : _resource(std::move(rv._resource)), _release(std::move(rv._release)) {
        rv._commit = false; //控制右值对象析构时不再执行_release
    }
    /* 对象析构时根据_commit标志执行_release函数 */
    ~RAIIVar() noexcept {
        if (_commit) _release(_resource);
    }
    /* 设置_commit标志 */
    _Self &commit(bool c = true) noexcept {
        _commit = c;
        return *this;
    };
    T &get() noexcept { return _resource; }
    T &operator*() noexcept { return get(); }

    /* 根据 T类型不同选择不同的->操作符模板 */
    template <typename _T = T> typename std::enable_if<std::is_pointer<_T>::value, _T>::type operator->() const noexcept { return _resource; }
    template <typename _T = T> typename std::enable_if<std::is_class<_T>::value, _T *>::type operator->() const noexcept {
        return std::addressof(_resource);
    }

private:
    /* 为true时析构函数执行release */
    bool _commit = true;
    T _resource;
    rel_type _release;
};
/* 创建 RAII 对象,
 * 用std::bind将M_REL,M_ACQ封装成std::function<void()>创建raii对象
 * RES      资源类型
 * M_REL    释放资源的成员函数地址
 * M_ACQ    申请资源的成员函数地址
 */
template <typename RES, typename M_REL, typename M_ACQ> RAII make_raii(RES &res, M_REL rel, M_ACQ acq, bool default_com = true) noexcept {
    // 编译时检查参数类型
    // 静态断言中用到的is_class,is_member_function_pointer等是用于编译期的计算、查询、判断、转换的type_traits类,
    // 有点类似于java的反射(reflect)提供的功能,不过只能用于编译期，不能用于运行时。
    // 关于type_traits的详细内容参见:http://www.cplusplus.com/reference/type_traits/
    static_assert(std::is_class<RES>::value, "RES is not a class or struct type.");
    static_assert(std::is_member_function_pointer<M_REL>::value, "M_REL is not a member function.");
    static_assert(std::is_member_function_pointer<M_ACQ>::value, "M_ACQ is not a member function.");
    assert(nullptr != rel && nullptr != acq);
    auto p_res = std::addressof(const_cast<typename no_const<RES>::type &>(res));
    return RAII(std::bind(rel, p_res), std::bind(acq, p_res), default_com);
}
/* 创建 RAII 对象 无需M_ACQ的简化版本 */
template <typename RES, typename M_REL> RAII make_raii(RES &res, M_REL rel, bool default_com = true) noexcept {
    static_assert(std::is_class<RES>::value, "RES is not a class or struct type.");
    static_assert(std::is_member_function_pointer<M_REL>::value, "M_REL is not a member function.");
    assert(nullptr != rel);
    auto p_res = std::addressof(const_cast<typename no_const<RES>::type &>(res));
    return RAII(
        std::bind(rel, p_res), [] {}, default_com);
}
} // namespace common