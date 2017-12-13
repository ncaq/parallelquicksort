﻿#include <algorithm>				// for std::partition, std::shuffle, std::sort
#include <array>					// for std::array
#include <chrono>					// for std::chrono
#include <cstdint>					// for std::int32_t
#include <fstream>					// for std::ofstream
#include <iostream>					// for std::cout
#include <iterator>                 // for std::distance
#include <numeric>					// for std::iota
#include <random>					// for std::mt19937, std::random_device
#include <stack>					// for std::stack
#include <string>					// for std::string
#include <thread>					// for std::thread
#include <tuple>					// for std::tie
#include <utility>					// for std::pair
#include <vector>					// for std::vector

#if __INTEL_COMPILER >= 18
#include <pstl/algorithm>
#include <pstl/execution>			// for std::execution::par_unseq
#endif

#include <boost/assert.hpp>			// for boost::assert
#include <boost/format.hpp>			// for boost::format
#include <boost/thread.hpp>			// for boost::thread::physical_concurrency

#if defined(__INTEL_COMPILER) || __GNUC__ >= 5
#include <cilk/cilk.h>				// for cilk_spawn, cilk_sync
#endif

#include <tbb/parallel_invoke.h>	// for tbb::parallel_invoke
#include <tbb/parallel_sort.h>		// for tbb::parallel_sort

#define DEBUG

namespace {
    //! A enumerated type
    /*!
        パフォーマンスをチェックする際の対象配列の種類を定義した列挙型
    */
    enum class Checktype : std::int32_t {
        // 完全にランダムなデータ
        RANDOM = 0,

        // あらかじめソートされたデータ
        SORT = 1,

        // 最初の1/4だけソートされたデータ
        QUARTERSORT = 2
    };

    //! A global variable (constant expression).
    /*!
        計測する回数
    */
    static auto constexpr CHECKLOOP = 5;

    //! A global variable (constant expression).
    /*!
        ソートする配列の要素数の最初の数
    */
    static auto constexpr N = 500;

    //! A global variable (constant).
    /*!
        実行するCPUの物理コア数
    */
    static std::int32_t const NUMPHYSICALCORE = boost::thread::physical_concurrency();

    //! A global variable (constant).
    /*!
        クイックソートにおける閾値
    */
    static auto constexpr THRESHOLD = 500;
    
    //! A function.
    /*!
        並列化されたソート関数のパフォーマンスをチェックする
        \param checktype パフォーマンスをチェックする際の対象配列の種類
        \param ofs 出力用のファイルストリーム
    */
    void check_performance(Checktype checktype, std::ofstream & ofs);

    //! A function.
    /*!
        引数で与えられたstd::functionの実行時間をファイルに出力する
        \param func 実行するstd::function
        \param ofs 出力用のファイルストリーム
        \param vec funcに渡すstd::vector
    */
    void elapsed_time(std::function<void(std::vector<std::int32_t> &)> const & func, std::ofstream & ofs, std::vector<std::int32_t> & vec);

    template < class RandomIter >
    //! A template function.
    /*!
        指定された範囲の要素をクイックソートでソートする
        \param first 範囲の下限
        \param last 範囲の上限
    */
    void quick_sort(RandomIter first, RandomIter last)
    {
        using mypair = std::pair< RandomIter, RandomIter >;

        // 範囲の情報を格納するスタック
        std::stack< mypair, std::vector< mypair > > stack;

        // 範囲の上限と下限をスタックへ積む
        stack.push(std::make_pair(first, last - 1));

        // スタックが空になるまで繰り返す
        while (!stack.empty()) {
            // 範囲の情報をスタックから取り出す
            // C++17の構造化束縛を使いたいが
            // Intel C++ Compiler 18.0は未だに構造化束縛をサポートしていない
            RandomIter left, right;
            std::tie(left, right) = stack.top();
            stack.pop();

            auto i = left;
            auto j = right;
            auto const pivot = (*left + *right) / 2;

            // 左右から進めてきたiとjがぶつかるまでループ
            while (i <= j) {
                // 基準値以上の値が見つかるまで右方向へ進めていく
                while (*i < pivot) {
                    ++i;
                }

                // 基準値以下の値が見つかるまで左方向へ進めていく 
                while (*j > pivot) {
                    --j;
                }

                // 左右から進めてきたiとjがぶつかったら
                if (i <= j) {
                    // 基準値位置よりも左側にあり、基準値よりも大きい値と、
                    // 基準値位置よりも右側にあり、基準値よりも小さい値の
                    // 位置関係を交換する。
                    std::iter_swap(i, j);

                    // 次回に備えて、注目点をずらす
                    ++i;

                    // 境界チェック
                    if (j != first) {
                        // 次回に備えて、注目点をずらす
                        --j;
                    }
                }
            }

            // 左右の配列のうち、要素数が少ない方を先に処理する
            // 後で処理する側は、その範囲をスタックへ積んでおく
            if (left < j) {
                // 右側の配列を次のソート処理の対象にする
                stack.push(std::make_pair(left, j));
            }

            if (i < right) {
                // 左側の配列を次のソート処理の対象にする
                stack.push(std::make_pair(i, right));
            }
        }
    }

#if defined(__INTEL_COMPILER) || __GNUC__ >= 5
    template < class RandomIter >
    //! A template function.
    /*!
        指定された範囲の要素をクイックソートでソートする（Cilkで並列化）
        \param first 範囲の下限
        \param last 範囲の上限
        \param reci 現在の再帰の深さ
    */
    void quick_sort_cilk(RandomIter first, RandomIter last, std::int32_t reci)
    {
        // 部分ソートの要素数
        auto const num = std::distance(first, last);

        if (num <= 1) {
            // 部分ソートの要素数が0なら何もすることはない
            return;
        }

        // 再帰の深さ + 1
        reci++;

        // 部分ソートが小さくなりすぎるとシリアル実行のほうが効率が良くなるため
        // 部分ソートの要素数が閾値以上の時だけ再帰させる
        // かつ、現在の再帰の深さが物理コア数以下のときだけ再帰させる
        if (num >= THRESHOLD && reci <= NUMPHYSICALCORE) {
            // 交点まで左右から入れ替えして交点を探す
            auto const middle = std::partition(first + 1, last, [first](auto n) { return n < *first; });
            
            // 交点 - 1の位置
            auto const mid = middle - 1;

            // 交点を移動
            std::iter_swap(first, mid);
            
            // 下部をソート（別スレッドで実行）
            cilk_spawn quick_sort_cilk(first, mid, reci);

            // 上部をソート（別スレッドで実行）
            cilk_spawn quick_sort_cilk(middle, last, reci);

            // 二つのスレッドの終了を待機
            cilk_sync;
        }
        else {
            // 再帰なしのクイックソートの関数を呼び出す
            quick_sort(first, last);
        }
    }

    template < class RandomIter >
    //! A template function.
    /*!
        指定された範囲の要素をクイックソートでソートする（Cilkで並列化）
        \param first 範囲の下限
        \param last 範囲の上限
    */
    inline void quick_sort_cilk(RandomIter first, RandomIter last)
    {
        // 再帰ありの並列クイックソートの関数を呼び出す
        quick_sort_cilk(first, last, 0);
    }
#endif

#if _OPENMP >= 200805
    template < class RandomIter >
    //! A template function.
    /*!
        指定された範囲の要素をクイックソートでソートする（OpenMPで並列化）
        \param first 範囲の下限
        \param last 範囲の上限
        \param reci 現在の再帰の深さ
    */
    void quick_sort_openmp(RandomIter first, RandomIter last, std::int32_t reci)
    {
        // 部分ソートの要素数
        auto const num = std::distance(first, last);

        if (num <= 1) {
            // 部分ソートの要素数が0なら何もすることはない
            return;
        }

        // 再帰の深さ + 1
        reci++;

        // 部分ソートが小さくなりすぎるとシリアル実行のほうが効率が良くなるため
        // 部分ソートの要素数が閾値以上の時だけ再帰させる
        // かつ、現在の再帰の深さが物理コア数以下のときだけ再帰させる
        if (num >= THRESHOLD && reci <= NUMPHYSICALCORE) {
            // 交点まで左右から入れ替えして交点を探す
            auto const middle = std::partition(first + 1, last, [first](auto n) { return n < *first; });

            // 交点 - 1の位置
            auto const mid = middle - 1;
            
            // 交点を移動
            std::iter_swap(first, mid);

            // 次の関数をタスクとして実行
#pragma omp task
            // 下部をソート
            quick_sort_openmp(first, mid, reci);

            // 次の関数をタスクとして実行
#pragma omp task
            // 上部をソート
            quick_sort_openmp(middle, last, reci);

            // 二つのタスクの終了を待機
#pragma omp taskwait
        }
        else {
            // 再帰なしのクイックソートの関数を呼び出す
            quick_sort(first, last);
        }
    }

    template < class RandomIter >
    //! A template function.
    /*!
        指定された範囲の要素をクイックソートでソートする（OpenMPで並列化）
        \param first 範囲の下限
        \param last 範囲の上限
    */
    inline void quick_sort_openmp(RandomIter first, RandomIter last)
    {
#pragma omp parallel    // OpenMP並列領域の始まり
#pragma omp single      // task句はsingle領域で実行
        // 再帰ありの並列クイックソートを呼び出す
        quick_sort_openmp(first, last, 0);
    }
#endif

    template < class RandomIter >
    //! A template function.
    /*!
        指定された範囲の要素をクイックソートでソートする（TBBで並列化）
        \param first 範囲の下限
        \param last 範囲の上限
        \param reci 現在の再帰の深さ
    */
    void quick_sort_tbb(RandomIter first, RandomIter last, std::int32_t reci)
    {
        // 部分ソートの要素数
        auto const num = std::distance(first, last);

        if (num <= 1) {
            // 部分ソートの要素数が0なら何もすることはない
            return;
        }

        // 再帰の深さ + 1
        reci++;

        // 部分ソートが小さくなりすぎるとシリアル実行のほうが効率が良くなるため
        // 部分ソートの要素数が閾値以上の時だけ再帰させる
        // かつ、現在の再帰の深さが物理コア数以下のときだけ再帰させる
        if (num >= THRESHOLD && reci <= NUMPHYSICALCORE) {
            // 交点まで左右から入れ替えして交点を探す
            auto const middle = std::partition(first + 1, last, [first](auto n) { return n < *first; });
            
            // 交点 - 1の位置
            auto const mid = middle - 1;

            // 交点を移動
            std::iter_swap(first, mid);

            // 二つのラムダ式を別スレッドで実行
            tbb::parallel_invoke(
                    // 下部をソート
                    [first, mid, reci]() { quick_sort_tbb(first, mid, reci); },
                    // 上部をソート
                    [middle, last, reci]() { quick_sort_tbb(middle, last, reci); });
        }
        else {
            // 再帰なしのクイックソートの関数を呼び出す
            quick_sort(first, last);
        }
    }

    template < class RandomIter >
    //! A template function.
    /*!
        指定された範囲の要素をクイックソートでソートする（TBBで並列化）
        \param first 範囲の下限
        \param last 範囲の上限
    */
    inline void quick_sort_tbb(RandomIter first, RandomIter last)
    {
        // 再帰ありの並列クイックソートの関数を呼び出す
        quick_sort_tbb(first, last, 0);
    }

    template < class RandomIter >
    //! A template function.
    /*!
        指定された範囲の要素をクイックソートでソートする（std::threadで並列化）
        \param first 範囲の下限
        \param last 範囲の上限
        \param reci 現在の再帰の深さ
    */
    void quick_sort_thread(RandomIter first, RandomIter last, std::int32_t reci)
    {
        // 部分ソートの要素数
        auto const num = std::distance(first, last);

        if (num <= 1) {
            // 部分ソートの要素数が0なら何もすることはない
            return;
        }

        // 再帰の深さ + 1
        reci++;

        // 部分ソートが小さくなりすぎるとシリアル実行のほうが効率が良くなるため
        // 部分ソートの要素数が閾値以上の時だけ再帰させる
        // かつ、現在の再帰の深さが物理コア数以下のときだけ再帰させる
        if (num >= THRESHOLD && reci <= NUMPHYSICALCORE) {
            // 交点まで左右から入れ替えして交点を探す
            auto const middle = std::partition(first + 1, last, [first](auto n) { return n < *first; });
            
            // 交点 - 1の位置
            auto const mid = middle - 1;

            // 交点を移動
            std::iter_swap(first, mid);

            // 下部をソート（別スレッドで実行）
            auto th1 = std::thread([first, mid, reci]() { quick_sort_thread(first, mid, reci); });
            
            // 上部をソート（別スレッドで実行）
            auto th2 = std::thread([middle, last, reci]() { quick_sort_thread(middle, last, reci); });
            
            // 二つのスレッドの終了を待機
            th1.join();
            th2.join();
        }
        else {
            // 再帰なしのクイックソートの関数を呼び出す
            quick_sort(first, last);
        }
    }

    template < class RandomIter >
    //! A template function.
    /*!
        指定された範囲の要素をクイックソートでソートする（std::threadで並列化）
        \param first 範囲の下限
        \param last 範囲の上限
    */
    inline void quick_sort_thread(RandomIter first, RandomIter last)
    {
        // 再帰ありの並列クイックソートの関数を呼び出す
        quick_sort_thread(first, last, 0);
    }

#ifdef DEBUG
    //! A template function.
    /*!
        与えられた二つのstd::vectorのすべての要素が同じかどうかチェックする
        \param v1 一つ目のstd::vector
        \param v2 二つめのstd::vector
        \return 与えられた二つのstd::vectorのすべての要素が同じならtrue、そうでないならfalse
    */
    bool vec_check(std::vector<std::int32_t> const & v1, std::vector<std::int32_t> const & v2);
#endif
}

int main()
{
    std::cout << "物理コア数: " << boost::thread::physical_concurrency();
    std::cout << ", 論理コア数: " << boost::thread::hardware_concurrency() << std::endl;

    std::ofstream ofsrandom("完全にシャッフルされたデータ.csv");
    std::ofstream ofssort("あらかじめソートされたデータ.csv");
    std::ofstream ofsquartersort("最初の1_4だけソートされたデータ.csv");
    
    std::cout << "完全にシャッフルされたデータを計測中...\n";
    check_performance(Checktype::RANDOM, ofsrandom);

    std::cout << "\nあらかじめソートされたデータを計測中...\n";
    check_performance(Checktype::SORT, ofssort);

    std::cout << "\n最初の1_4だけソートされたデータを計測中...\n";
    check_performance(Checktype::QUARTERSORT, ofsquartersort);

    return 0;
}

namespace {
    void check_performance(Checktype checktype, std::ofstream & ofs)
    {
        ofs << "配列の要素数,std::sort,クイックソート,std::thread,OpenMP,TBB,Cilk,tbb::parallel_sort,std::sort (Parallelism TS)\n";

        auto n = N;
        for (auto i = 0; i < 6; i++) {
            for (auto j = 0; j < 2; j++) {
                std::cout << n << "個を計測中\n";

                std::vector<std::int32_t> vec(n);
                std::iota(vec.begin(), vec.end(), 1);
                std::vector<std::int32_t> vecback(vec);

                switch (checktype) {
                    case Checktype::RANDOM:
                    {
                        std::random_device rnd;
                        std::shuffle(vec.begin(), vec.end(), std::mt19937(rnd()));
                    }
                    break;

                    case Checktype::SORT:
                        break;

                    case Checktype::QUARTERSORT:
                    {
                        std::random_device rnd;
                        std::shuffle(vec.begin() + n / 4, vec.end(), std::mt19937(rnd()));
                    }
                    break;

                    default:
                        BOOST_ASSERT(!"switchのdefaultに来てしまった！");
                        break;
                }
                
                std::array< std::vector<std::int32_t>, 8 > vecar = { vec, vec, vec, vec, vec, vec, vec, vec };

                ofs << n << ',';

                elapsed_time([](auto & vec) { std::sort(vec.begin(), vec.end()); }, ofs, vecar[0]);
                elapsed_time([](auto & vec) { quick_sort(vec.begin(), vec.end()); }, ofs, vecar[1]);
                elapsed_time([](auto & vec) { quick_sort_thread(vec.begin(), vec.end()); }, ofs, vecar[2]);

#if _OPENMP >= 200805
                elapsed_time([](auto & vec) { quick_sort_openmp(vec.begin(), vec.end()); }, ofs, vecar[3]);
#endif
                elapsed_time([](auto & vec) { quick_sort_tbb(vec.begin(), vec.end()); }, ofs, vecar[4]);

#if defined(__INTEL_COMPILER) || __GNUC__ >= 5
                elapsed_time([](auto & vec) { quick_sort_cilk(vec.begin(), vec.end()); }, ofs, vecar[5]);
#endif
                elapsed_time([](auto & vec) { tbb::parallel_sort(vec); }, ofs, vecar[6]);

#if __INTEL_COMPILER >= 18
                elapsed_time([](auto & vec) { std::sort(std::execution::par, vec.begin(), vec.end()); }, ofs, vecar[7]);
#endif
                ofs << std::endl;

#ifdef DEBUG
                for (auto i = 0U; i < vecar.size(); i++) {
#if _OPENMP < 200805
                    if (i == 3) {
                        continue;
                    }
#endif

#if !defined(__INTEL_COMPILER) && __GNUC__ < 5
                    if (i == 5) {
                        continue;
                    }                    
#endif

#if __INTEL_COMPILER < 18
                    if (i == 7) {
                        continue;
                    }                    
#endif

                    if (!vec_check(vecback, vecar[i])) {
                        std::cout << "Error! vecar[" << i << ']' << std::endl;
                    }
                }
#endif
                if (!j) {
                    n *= 2;
                }
            }

            n *= 5;
        }
    }

    void elapsed_time(std::function<void(std::vector<std::int32_t> &)> const & func, std::ofstream & ofs, std::vector<std::int32_t> & vec)
    {
        using namespace std::chrono;

        auto vback(vec);

        auto elapsed_time = 0.0;
        for (auto i = 1; i <= CHECKLOOP; i++) {
            auto beg = high_resolution_clock::now();
            func(vec);
            auto end = high_resolution_clock::now();
        
            elapsed_time += (duration_cast<duration<double>>(end - beg)).count();

            if (i != CHECKLOOP) {
                vec.assign(vback.begin(), vback.end());
            }
        }


        ofs << boost::format("%.10f") % (elapsed_time / static_cast<double>(CHECKLOOP)) << ',';
    }
    
#ifdef DEBUG
    bool vec_check(std::vector<std::int32_t> const & v1, std::vector<std::int32_t> const & v2)
    {
        for (auto i = 0; i < N; i++) {
            if (v1[i] != v2[i]) {
                std::cerr << "Error! i = " << i << std::endl;
                return false;
            }
        }

        return true;
    }
#endif
}