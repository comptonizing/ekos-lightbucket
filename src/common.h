#pragma once

#include <mutex>

#define DEBUGMSG(...)                               \
  do {                                      \
    if ( Config::instance().isDebug() )                              \
    {                                       \
      printf("DEBUG: ");                 \
      printf(__VA_ARGS__);                              \
      printf("\n");                             \
    }                                       \
  } while (0) ;

#define TINY 1.0e-15F
#define HUGE 9.9999e99

// #define pixfloat double
// #define fitsfloat TDOUBLE

template <typename T>
void reverseRowOrder(T *arr, long dimX, long  dimY)
{
    for (long ii=0; ii<dimX; ii++) {
        for (long low = 0, high = dimY - 1; low < high; low++, high--)
        {
            long iLo = ii + dimX * low;
            long iHi = ii + dimX * high;
            T temp = arr[iLo];
            arr[iLo] = arr[iHi];
            arr[iHi] = temp;
        }
    }
}

template <typename T>
class SerialProperty {
    public:
        mutable std::mutex m_mutex;
        SerialProperty() { m_isSet = false; }
        SerialProperty(T value) : m_value(value) { m_isSet = true; }
        void reset() { m_isSet = false; }
        T get() const {
            return m_value;
        }
        void set(T value) {
            m_isSet = true;
            m_value = value;
        }
        operator T() { return get(); }
        SerialProperty(const SerialProperty &other) {
            m_value = other.get();
        }
        SerialProperty& operator=(SerialProperty const &other) {
            if ( this == &other ) {
                return *this;
            }
            this->set(other.get());
            return *this;
        }
        bool isSet() {
            return m_isSet;
        }
        void lock() {
            m_mutex.lock();
        }
        void unlock() {
            m_mutex.unlock();
        }

    private:
        T m_value;
        bool m_isSet = false;
};
