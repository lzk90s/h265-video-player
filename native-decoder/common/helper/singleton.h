#pragma once

namespace common {

template <class T> class Singleton {
public:
    static T &getInstance() {
        static T value;
        return value;
    }

private:
    Singleton(){};
    ~Singleton(){};
};
} // namespace common