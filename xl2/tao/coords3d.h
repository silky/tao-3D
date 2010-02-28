#ifndef COORDS3D_H
#define COORDS3D_H
// ****************************************************************************
//  coords3d.h                                                      Tao project
// ****************************************************************************
// 
//   File Description:
// 
//     Basic operations on 3D coordinates (3-vectors, 3-points)
// 
// 
// 
// 
// 
// 
// 
// 
// ****************************************************************************
// This document is released under the GNU General Public License.
// See http://www.gnu.org/copyleft/gpl.html and Matthew 25:22 for details
//  (C) 1992-2010 Christophe de Dinechin <christophe@taodyne.com>
//  (C) 2010 Taodyne SAS
// ****************************************************************************

#include "base.h"
#include "tao.h"
#include <cmath>

TAO_BEGIN

// ============================================================================
// 
//   Types defined in this file
// 
// ============================================================================

typedef double coord;           // Coordinate type
typedef double scale;           // Scale type
struct Point;                   // 2D point
struct Vector;                  // 2D vector
struct Point3;                  // 3D point
struct Vector3;                 // 3D vector


// ============================================================================
// 
//   Point and Point3 class
// 
// ============================================================================

struct Point
// ----------------------------------------------------------------------------
//    A 2D dimensional point
// ----------------------------------------------------------------------------
{
    Point(): x(0), y(0) {}
    Point(coord X, coord Y): x(X), y(Y) {}
    Point(const Point &o): x(o.x), y(o.y) {}
    Point &Set(coord X, coord Y) { x = X; y = Y; return *this; }
    Point& operator = (const Point& o)
    {
        x = o.x;
        y = o.y;
        return *this;
    }
    bool operator == (const Point&o) const
    {
        return x == o.x && y == o.y;
    }
    bool operator != (const Point&o) const
    {
        return ! operator ==(o);
    }

    Point& operator +=(const Vector& o);
    Point& operator -=(const Vector& o);

public:
    coord x, y;
};


struct Point3
// ----------------------------------------------------------------------------
//    A 3D dimensional point
// ----------------------------------------------------------------------------
{
    Point3(): x(0), y(0), z(0) {}
    Point3(coord X, coord Y, coord Z): x(X), y(Y), z(Z) {}
    Point3(const Point3 &o): x(o.x), y(o.y), z(o.z) {}
    Point3 &Set(coord X, coord Y, coord Z) { x=X; y=Y; z=Z; return *this; }
    Point3& operator = (const Point3& o)
    {
        x = o.x;
        y = o.y;
        z = o.z;
        return *this;
    }
    bool operator == (const Point3&o) const
    {
        return x == o.x && y == o.y && z == o.z;
    }
    bool operator != (const Point3&o) const
    {
        return ! operator ==(o);
    }

    Point3& operator +=(const Vector3& o);
    Point3& operator -=(const Vector3& o);

public:
    coord x, y, z;
};


// ============================================================================
//
//   Vector and Vector3 class
//
// ============================================================================

struct Vector : Point
// ----------------------------------------------------------------------------
//    A three-dimensional vector
// ----------------------------------------------------------------------------
{
    Vector(): Point() {}
    Vector(coord x = 0.0, coord y = 0.0): Point(x,y) {}
    Vector(const Vector &o): Point(o) {}

    Vector& operator +=(const Vector& o)
    {
        x += o.x;
        y += o.y;
        return *this;
    }

    Vector& operator -=(const Vector& o)
    {
        x -= o.x;
        y -= o.y;
        return *this;
    }

    Vector& operator *=(scale s)
    {
        x *= s;
        y *= s;
        return *this;
    }
    
    Vector& operator /=(scale s)
    {
        x /= s;
        y /= s;
        return *this;
    }

    scale Length() const
    {
        return sqrt(x*x+y*y);
    }

    Vector& Normalize()
    {
        *this /= Length();
        return *this;
    }
};


struct Vector3 : Point3
// ----------------------------------------------------------------------------
//    A three-dimensional vector
// ----------------------------------------------------------------------------
{
    Vector3(): Point3() {}
    Vector3(coord x = 0.0, coord y = 0.0, coord z = 0.0): Point3(x,y,z) {}
    Vector3(const Vector3 &o): Point3(o) {}

    Vector3& operator +=(const Vector3& o)
    {
        x += o.x;
        y += o.y;
        z += o.z;
        return *this;
    }

    Vector3& operator -=(const Vector3& o)
    {
        x -= o.x;
        y -= o.y;
        z -= o.z;
        return *this;
    }

    Vector3& operator *=(scale s)
    {
        x *= s;
        y *= s;
        z *= s;
        return *this;
    }
    
    Vector3& operator /=(scale s)
    {
        x /= s;
        y /= s;
        z /= s;
        return *this;
    }

    Vector3& Cross(const Vector3& o)
    {
        coord cx = y*o.z - z*o.y;
        coord cy = z*o.x - x*o.z;
        coord cz = x*o.y - y*o.x;
        x = cx; y = cy; z = cz;
        return *this;
    }
    scale Length() const
    {
        return sqrt(x*x+y*y+z*z);
    }

    Vector3& Normalize()
    {
        *this /= Length();
        return *this;
    }
};



// ============================================================================
//
//   Inline Point and Vector operations not defined in class
//
// ============================================================================

inline Point& Point::operator +=(const Vector& o)
{
    x += o.x;
    y += o.y;
    return *this;
}

inline Point& Point::operator -=(const Vector& o)
{
    x -= o.x;
    y -= o.y;
    return *this;
}

inline Point operator +(const Point& l, const Vector &r)
{
    Point result(l);
    result += r;
    return result;
}

inline Vector operator -(const Point& l, const Point &r)
{
    Vector result(l.x-r.x, l.y-r.y);
    return result;
}

inline Vector operator +(const Vector& l, const Vector &r)
{
    Vector result(l);
    result += r;
    return result;
}
inline Vector operator -(const Vector& l, const Vector &r)
{
    Vector result(l);
    result -= r;
    return result;
}

inline Vector operator *(const Vector& l, coord s)
{
    Vector result(l);
    result *= s;
    return result;
}

inline Vector operator /(const Vector& l, coord s)
{
    Vector result(l);
    result /= s;
    return result;
}

inline coord operator* (const Vector& l, const Vector& r)
{
    return l.x*r.x + l.y*r.y;
}



// ============================================================================
//
//   Inline Point3 and Vector3 operations not defined in class
//
// ============================================================================

inline Point3& Point3::operator +=(const Vector3& o)
{
    x += o.x;
    y += o.y;
    z += o.z;
    return *this;
}

inline Point3& Point3::operator -=(const Vector3& o)
{
    x -= o.x;
    y -= o.y;
    z -= o.z;
    return *this;
}

inline Point3 operator +(const Point3& l, const Vector3 &r)
{
    Point3 result(l);
    result += r;
    return result;
}

inline Vector3 operator -(const Point3& l, const Point3 &r)
{
    Vector3 result(l.x-r.x, l.y-r.y, l.x-r.z);
    return result;
}

inline Vector3 operator +(const Vector3& l, const Vector3 &r)
{
    Vector3 result(l);
    result += r;
    return result;
}
inline Vector3 operator -(const Vector3& l, const Vector3 &r)
{
    Vector3 result(l);
    result -= r;
    return result;
}

inline Vector3 operator *(const Vector3& l, coord s)
{
    Vector3 result(l);
    result *= s;
    return result;
}

inline Vector3 operator /(const Vector3& l, coord s)
{
    Vector3 result(l);
    result /= s;
    return result;
}

inline coord operator* (const Vector3& l, const Vector3& r)
{
    return l.x*r.x + l.y*r.y + l.z*r.z;
}

inline Vector3 operator^ (const Vector3& l, const Vector3 &r)
{
    Vector3 result(l);
    return result.Cross(r);
}



// ============================================================================
//
//    Box and Box3 class
//
// ============================================================================

struct Box
// ----------------------------------------------------------------------------
//   A simple 2D box, used most often as a bounding box
// ----------------------------------------------------------------------------
{
    Box(): lower(inf, inf), upper(-inf, -inf) {}
    Box(const Point &l, const Point &u): lower(l), upper(u) {}
    Box(const Point &l, const Vector &s): lower(l), upper(l+s) {}
    Box(coord x, coord y, coord w, coord h): lower(x, y), upper(x+w, y+h) {}

    Box & operator = (const Box &o)
    {
        lower = o.lower;
        upper = o.upper;
        return *this;
    }

    bool operator == (const Box &o) const
    {
        return lower == o.lower && upper == o.upper;
    }

    bool operator != (const Box &o) const
    {
        return ! operator==(o);
    }

    bool operator <= (const Box &o) const
    {
        // Check if box is included in other box (not a total order)
        return lower.x >= o.lower.x
            && lower.y >= o.lower.y
            && upper.x <= o.upper.x
            && upper.y <= o.upper.y;
    }

    bool operator < (const Box &o) const
    {
        // Check if box is included strictly in other box
        return lower.x > o.lower.x
            && lower.y > o.lower.y
            && upper.x < o.upper.x
            && upper.y < o.upper.y;
    }

    bool operator >= (const Box &o) const
    {
        return o.operator <= (*this);
    }

    bool operator > (const Box &o) const
    {
        return o.operator < (*this);
    }

    Box & operator += (const Vector &o)
    {
        lower += o;
        upper += o;
        return *this;
    }

    Box & operator -= (const Vector &o)
    {
        lower -= o;
        upper -= o;
        return *this;
    }

    Box & operator *= (scale s)
    {
        // Scale box around its center
        Point center = Center();
        lower = center + (lower - center) * s;
        upper = center + (upper - center) * s;
        return *this;
    }

    Box &operator |= (const Box &o)
    {
        // Return union of the two boxes (smallest box containing both)
        if (o.lower.x < lower.x)        lower.x = o.lower.x;
        if (o.lower.y < lower.y)        lower.y = o.lower.y;

        if (o.upper.x > upper.x)        upper.x = o.upper.x;
        if (o.upper.y > upper.y)        upper.y = o.upper.y;

        return *this;
    }

    Box &operator &= (const Box &o)
    {
        // Return intersection of the two boxes (biggest box contained in both)
        if (o.lower.x > lower.x)        lower.x = o.lower.x;
        if (o.lower.y > lower.y)        lower.y = o.lower.y;

        if (o.upper.x < upper.x)        upper.x = o.upper.x;
        if (o.upper.y < upper.y)        upper.y = o.upper.y;

        return *this;
    }

    Box &Empty()
    {
        lower = Point();
        upper = Point();
        return *this;
    }

    bool IsEmpty() const
    {
        return lower.x >= upper.x
            || lower.y >= upper.y;
    }

    Box &Normalize()
    {
        if (lower.x > upper.x)  std::swap(lower.x, upper.x);
        if (lower.y > upper.y)  std::swap(lower.y, upper.y);
        return *this;
    }

    Point UpperLeft() const
    {
        return Point(lower.x, upper.y);
    }
    Point UpperRight() const
    {
        return upper;
    }
    Point LowerLeft() const
    {
        return lower;
    }
    Point LowerRight() const
    {
        return Point(upper.x, lower.y);
    }
    Point Center() const
    {
        return Point((lower.x + upper.x)/2, (lower.y + upper.y)/2);
    }
    coord Width() const      { return upper.x - lower.x; }
    coord Height() const     { return upper.y - lower.y; }
    coord Left() const       { return lower.x; }
    coord Right() const      { return upper.x; }
    coord Top() const        { return upper.y; }
    coord Bottom() const     { return lower.y; }

public:
    Point lower, upper;
    static const coord inf = 1e32;
};


struct Box3
// ----------------------------------------------------------------------------
//   A simple 3D box, used most often as a bounding box
// ----------------------------------------------------------------------------
{
    Box3(): lower(inf, inf, inf), upper(-inf, -inf, -inf) {}
    Box3(const Point3 &l, const Point3 &u): lower(l), upper(u) {}
    Box3(const Point3 &l, const Vector3 &s): lower(l), upper(l+s) {}
    Box3(coord x, coord y, coord z, coord w, coord h, coord d)
        : lower(x,y,z), upper(x+w, y+h, z+d) {}

    Box3 & operator = (const Box3 &o)
    {
        lower = o.lower;
        upper = o.upper;
        return *this;
    }

    bool operator == (const Box3 &o) const
    {
        return lower == o.lower && upper == o.upper;
    }

    bool operator != (const Box3 &o) const
    {
        return ! operator==(o);
    }

    bool operator <= (const Box3 &o) const
    {
        // Check if box is included in other box (not a total order)
        return lower.x >= o.lower.x
            && lower.y >= o.lower.y
            && lower.z >= o.lower.z
            && upper.x <= o.upper.x
            && upper.y <= o.upper.y
            && upper.z <= o.upper.z;
    }

    bool operator < (const Box3 &o) const
    {
        // Check if box is included strictly in other box
        return lower.x > o.lower.x
            && lower.y > o.lower.y
            && lower.z > o.lower.z
            && upper.x < o.upper.x
            && upper.y < o.upper.y
            && upper.z < o.upper.z;
    }

    bool operator >= (const Box3 &o) const
    {
        return o.operator <= (*this);
    }

    bool operator > (const Box3 &o) const
    {
        return o.operator < (*this);
    }

    Box3 & operator += (const Vector3 &o)
    {
        lower += o;
        upper += o;
        return *this;
    }

    Box3 & operator -= (const Vector3 &o)
    {
        lower -= o;
        upper -= o;
        return *this;
    }

    Box3 & operator *= (scale s)
    {
        // Scale box around its center
        Point3 center = Center();
        lower = center + (lower - center) * s;
        upper = center + (upper - center) * s;
        return *this;
    }

    Box3 &operator |= (const Box3 &o)
    {
        // Return union of the two boxes (smallest box containing both)
        if (o.lower.x < lower.x)        lower.x = o.lower.x;
        if (o.lower.y < lower.y)        lower.y = o.lower.y;
        if (o.lower.z < lower.z)        lower.z = o.lower.z;

        if (o.upper.x > upper.x)        upper.x = o.upper.x;
        if (o.upper.y > upper.y)        upper.y = o.upper.y;
        if (o.upper.z > upper.z)        upper.z = o.upper.z;

        return *this;
    }

    Box3 &operator &= (const Box3 &o)
    {
        // Return intersection of the two boxes (biggest box contained in both)
        if (o.lower.x > lower.x)        lower.x = o.lower.x;
        if (o.lower.y > lower.y)        lower.y = o.lower.y;
        if (o.lower.z > lower.z)        lower.z = o.lower.z;

        if (o.upper.x < upper.x)        upper.x = o.upper.x;
        if (o.upper.y < upper.y)        upper.y = o.upper.y;
        if (o.upper.z < upper.z)        upper.z = o.upper.z;

        return *this;
    }

    Box3 &Empty()
    {
        lower = Point3();
        upper = Point3();
        return *this;
    }

    bool IsEmpty() const
    {
        return lower.x >= upper.x
            || lower.y >= upper.y
            || lower.z >= upper.z;
    }
            
    Box3 &Normalize()
    {
        if (lower.x > upper.x)  std::swap(lower.x, upper.x);
        if (lower.y > upper.y)  std::swap(lower.y, upper.y);
        if (lower.z > upper.z)  std::swap(lower.z, upper.z);
        return *this;
    }
    Point3 Center() const
    {
        return Point3((lower.x + upper.x)/2,
                      (lower.y + upper.y)/2,
                      (lower.z + upper.z)/2);
    }
    coord Width() const       { return upper.x - lower.x; }
    coord Height() const      { return upper.y - lower.y; }
    coord Depth() const       { return upper.z - lower.z; }
    coord Left() const        { return lower.x; }
    coord Right() const       { return upper.x; }
    coord Top() const         { return upper.y; }
    coord Bottom() const      { return lower.y; }
    coord Front() const       { return upper.z; }
    coord Back() const        { return lower.z; }

public:
    Point3 lower, upper;
    static const coord inf = 1e33;
};



// ============================================================================
// 
//   Inline operations not defined in class
// 
// ============================================================================

inline Box operator+ (const Box &b, const Vector &v)
{
    Box result(b);
    result += v;
    return result;
}


inline Box3 operator+ (const Box3 &b, const Vector3 &v)
{
    Box3 result(b);
    result += v;
    return result;
}


TAO_END

#endif // VEC3_H
