#pragma once

#include "MovingObjectTypes.h"
#include "ObjectBase.h"
#include "SimArena.h"

#include <memory>
#include <vector>


//object container
class CObjectContainer {
public:
    CObjectContainer() { m_list.reserve(MAXOBJECTS); }

    // Takes ownership of `obj`. Returns the stored object (non-owning), or
    // nullptr if the container is full, in which case `obj` has been destroyed.
    // Never keep using the pointer you passed in: use the returned one.
    template<typename T>
    T* add(std::unique_ptr<T> obj) {
        T* raw = obj.get();
        return addObject(std::move(obj)) ? raw : nullptr;
    }
    // Same as above, for `add(new Foo(...))` call sites.
    template<typename T>
    T* add(T* obj) {
        return add(std::unique_ptr<T>(obj));
    }

    // True if add() would reject a new object.
    bool isFull() const { return m_list.size() + 1 >= m_list.capacity(); }

    void update() const;
    void draw() const;

    void clean();
    void cleanDeadObjects();

    float getClosestObject(short x, short y, ObjectType objectType) const;
    float getClosestMovingObject(short x, short y, MovingObjectType movingObjectType) const;
    size_t countTypes(ObjectType type) const;
    size_t countMovingTypes(MovingObjectType type) const;

    using List = sim::vector<std::unique_ptr<CObject>>;
    const List& list() const { return m_list; }

private:
    static constexpr size_t MAXOBJECTS = 300;

    bool addObject(std::unique_ptr<CObject> obj);

    List m_list;    // the buffer lives in the simulation arena
};
