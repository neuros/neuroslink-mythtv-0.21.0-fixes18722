// -*- Mode: c++ -*-

#ifndef __MYTH_DEQUE_H__
#define __MYTH_DEQUE_H__

#include <deque>
using namespace std;

/** \class MythDeque
 *  \brief MythDeque is similar to QPtrQueue, while being based off
 *         deque, this allows that items that are not at the head of
 *         the queue can be seen/deleted.
 */
template<typename T>
class MythDeque : public deque<T>
{
  public:
    /// \brief Removes item from front of list and returns a copy. O(1).
    T dequeue()
    {
        if (deque<T>::empty())
            return (T)(0);
        T item = deque<T>::front();
        deque<T>::pop_front();
        return item;
    }
    /// \brief Adds item to the back of the list. O(1).
    void enqueue(T d) { deque<T>::push_back(d); }

    typedef typename deque<T>::iterator iterator;
    typedef typename deque<T>::const_iterator const_iterator;
    typedef typename deque<T>::size_type size_type;

    /// \brief Finds an item in the list via linear search O(n).
    iterator find(T const item)
    {
        for (iterator it = deque<T>::begin(); it != deque<T>::end(); ++it)
            if (*it == item)
                return it;
        return deque<T>::end();
    }

    /// \brief Finds an item in the list via linear search O(n).
    const_iterator find(T const item) const
    {
        for (const_iterator it = deque<T>::begin(); it != deque<T>::end(); ++it)
            if (*it == item)
                return it;
        return deque<T>::end();
    }
    
    /// \brief Removes any item from list. O(n).
    void remove(T const item)
    {
        iterator it = find(item);
        if (it != deque<T>::end())
            deque<T>::erase(it);
    }

    /// \brief Returns true if item is in list. O(n).
    bool contains(T const item) const
        { return find(item) != deque<T>::end(); }

    /// \brief Returns size of list. O(1).
    size_type count() const { return deque<T>::size(); }

    /// \brief Returns item at head of list. O(1).
    T head() { return (deque<T>::size()) ? deque<T>::front() : (T)(NULL); }
    /// \brief Returns item at head of list. O(1).
    const T head() const
        { return (deque<T>::size()) ? deque<T>::front() : (T)(NULL); }

    /// \brief Returns item at tail of list. O(1).
    T tail() { return (deque<T>::size()) ? deque<T>::back() : (T)(NULL); }
    /// \brief Returns item at tail of list. O(1).
    const T tail() const
        { return (deque<T>::size()) ? deque<T>::back() : (T)(NULL); }
};

#endif // __MYTH_DEQUE_H__
