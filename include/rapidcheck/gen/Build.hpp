#pragma once

#include "rapidcheck/detail/ApplyTuple.h"

namespace rc {
namespace gen {
namespace detail {

template <typename T>
class Lens;

// Member variables
template <typename Type, typename T>
class Lens<T(Type::*)> {
public:
  typedef T(Type::*MemberPtr);
  typedef Type Target;
  typedef T ValueType;

  Lens(MemberPtr ptr)
      : m_ptr(ptr) {}

  void set(Target &obj, T &&arg) const { obj.*m_ptr = std::move(arg); }

private:
  MemberPtr m_ptr;
};

// Member functions with single argument
template <typename Type, typename Ret, typename T>
class Lens<Ret (Type::*)(T)> {
public:
  typedef Ret (Type::*MemberPtr)(T);
  typedef Type Target;
  typedef Decay<T> ValueType;

  Lens(MemberPtr ptr)
      : m_ptr(ptr) {}

  void set(Target &obj, ValueType &&arg) const { (obj.*m_ptr)(std::move(arg)); }

private:
  MemberPtr m_ptr;
};

// Member functions with multiple arguments
template <typename Type, typename Ret, typename T1, typename T2, typename... Ts>
class Lens<Ret (Type::*)(T1, T2, Ts...)> {
public:
  typedef Ret (Type::*MemberPtr)(T1, T2, Ts...);
  typedef Type Target;
  typedef std::tuple<Decay<T1>, Decay<T2>, Decay<Ts>...> ValueType;

  Lens(MemberPtr ptr)
      : m_ptr(ptr) {}

  void set(Target &obj, ValueType &&arg) const {
    rc::detail::applyTuple(
        std::move(arg),
        [&](Decay<T1> &&arg1, Decay<T2> &&arg2, Decay<Ts> &&... args) {
          (obj.*m_ptr)(std::move(arg1), std::move(arg2), std::move(args)...);
        });
  }

private:
  MemberPtr m_ptr;
};

template <typename Member>
struct Binding {
  using LensT = Lens<Member>;
  using Target = typename LensT::Target;
  using ValueType = typename LensT::ValueType;
  using GenT = Gen<ValueType>;

  Binding(LensT &&l, GenT &&g)
      : lens(std::move(l))
      , gen(std::move(g)) {}

  LensT lens;
  GenT gen;
};

template<typename T>
T &deref(T &x) { return x; }

template<typename T>
T &deref(std::unique_ptr<T> &x) { return *x; }

template<typename T>
T &deref(std::shared_ptr<T> &x) { return *x; }

template <typename T, typename Indexes, typename... Lenses>
class BuildMapper;

template <typename T, typename... Lenses, std::size_t... Indexes>
class BuildMapper<T, rc::detail::IndexSequence<Indexes...>, Lenses...> {
public:
  BuildMapper(const Lenses &... lenses)
      : m_lenses(std::move(lenses)...) {}

  T operator()(std::tuple<T, typename Lenses::ValueType...> &&tuple) const {
    T &obj = std::get<0>(tuple);
    auto dummy = {(std::get<Indexes>(m_lenses).set(
                       deref(obj), std::move(std::get<Indexes + 1>(tuple))),
                   0)...};

    return std::move(obj);
  }

private:
  std::tuple<Lenses...> m_lenses;
};

} // namespace detail

template <typename T, typename... Args>
Gen<T> construct(Gen<Args>... gens) {
  return gen::map(gen::tuple(std::move(gens)...),
                  [](std::tuple<Args...> &&argsTuple) {
                    return rc::detail::applyTuple(
                        std::move(argsTuple),
                        [](Args &&... args) { return T(std::move(args)...); });
                  });
}

template <typename T, typename Arg, typename... Args>
Gen<T> construct() {
  return gen::construct<T>(gen::arbitrary<Arg>(), gen::arbitrary<Args>()...);
}

template <typename T, typename... Args>
Gen<std::unique_ptr<T>> makeUnique(Gen<Args>... gens) {
  return gen::map(gen::tuple(std::move(gens)...),
                  [](std::tuple<Args...> &&argsTuple) {
                    return rc::detail::applyTuple(
                        std::move(argsTuple),
                        [](Args &&... args) {
                          return std::unique_ptr<T>(new T(std::move(args)...));
                        });
                  });
}

template <typename T, typename... Args>
Gen<std::shared_ptr<T>> makeShared(Gen<Args>... gens) {
  return gen::map(gen::tuple(std::move(gens)...),
                  [](std::tuple<Args...> &&argsTuple) {
                    return rc::detail::applyTuple(
                        std::move(argsTuple),
                        [](Args &&... args) {
                          return std::make_shared<T>(std::move(args)...);
                        });
                  });
}

template <typename Member>
detail::Binding<Member> set(Member member,
                            typename detail::Binding<Member>::GenT gen) {
  return detail::Binding<Member>(detail::Lens<Member>(member), std::move(gen));
}

template <typename Member>
detail::Binding<Member> set(Member member) {
  using T = typename detail::Binding<Member>::ValueType;
  return set(member, gen::arbitrary<T>());
}

template <typename T, typename... Members>
Gen<T> build(Gen<T> gen, const detail::Binding<Members> &... bs) {
  using Mapper =
      detail::BuildMapper<T,
                          rc::detail::MakeIndexSequence<sizeof...(Members)>,
                          typename detail::Binding<Members>::LensT...>;

  return gen::map(gen::tuple(std::move(gen), std::move(bs.gen)...),
                  Mapper(bs.lens...));
}

template <typename T, typename... Members>
Gen<T> build(const detail::Binding<Members> &... bs) {
  return build<T>(fn::constant(shrinkable::lambda([] { return T(); })), bs...);
}

} // namespace gen
} // namespace rc
