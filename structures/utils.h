#pragma once

template<typename T>
void operator+=(std::pair<T,T> &lhs, const std::pair<T,T> &rhs) {
    lhs.first += rhs.first;
    lhs.second += rhs.second;
}