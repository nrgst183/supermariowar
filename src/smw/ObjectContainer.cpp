#include "ObjectContainer.h"

#include "objects/moving/MovingObject.h"

#include <algorithm>
#include <limits>
#include <cmath>

//------------------------------------------------------------------------------
// class object_container
//------------------------------------------------------------------------------
void CObjectContainer::clean()
{
    m_list.clear();
}

void CObjectContainer::cleanDeadObjects()
{
    auto erase_from = std::remove_if(m_list.begin(), m_list.end(),
        [](const std::unique_ptr<CObject>& obj) { return obj->isDead(); });
    m_list.erase(erase_from, m_list.end());
}

bool CObjectContainer::addObject(std::unique_ptr<CObject> obj)
{
    // The list never grows past its reserved capacity, so adding an object
    // while update() or draw() is iterating never reallocates.
    if (isFull())
        return false;  // `obj` is destroyed here

    m_list.push_back(std::move(obj));
    return true;
}

// Objects may add new objects while being updated or drawn. Those are
// appended to the list and are first processed on the next call, so the
// number of objects to visit is fixed before the loop starts.
void CObjectContainer::update() const
{
    for (size_t i = 0, count = m_list.size(); i < count; i++)
        m_list[i]->update();
}

void CObjectContainer::draw() const
{
    for (size_t i = 0, count = m_list.size(); i < count; i++)
        m_list[i]->draw();
}

float CObjectContainer::getClosestObject(short ix, short iy, ObjectType objectType) const
{
    float minDist = std::numeric_limits<float>::max();

    for (const std::unique_ptr<CObject>& obj : m_list) {
        if (obj->getObjectType() != objectType)
            continue;

        short x = obj->x() - ix;
        short y = obj->y() - iy;

        float dist = x * x + y * y;
        if (dist < minDist)
            minDist = dist;
    }

    return ::sqrtf(minDist);
}

float CObjectContainer::getClosestMovingObject(short ix, short iy, MovingObjectType movingObjectType) const
{
    float minDist = std::numeric_limits<float>::max();

    for (const std::unique_ptr<CObject>& obj : m_list) {
        auto* movobj = dynamic_cast<IO_MovingObject*>(obj.get());
        if (!movobj || movobj->getMovingObjectType() != movingObjectType)
            continue;

        short x = obj->x() - ix;
        short y = obj->y() - iy;

        float dist = x * x + y * y;
        if (dist < minDist)
            minDist = dist;
    }

    return ::sqrtf(minDist);
}

size_t CObjectContainer::countTypes(ObjectType type) const
{
    size_t count = 0;

    for (const std::unique_ptr<CObject>& obj : m_list) {
        if (obj->getObjectType() == type) {
            count++;
        }
    }

    return count;
}

size_t CObjectContainer::countMovingTypes(MovingObjectType type) const
{
    size_t count = 0;

    for (const std::unique_ptr<CObject>& obj : m_list) {
        auto* movobj = dynamic_cast<IO_MovingObject*>(obj.get());
        if (movobj && movobj->getMovingObjectType() == type) {
            count++;
        }
    }

    return count;
}
