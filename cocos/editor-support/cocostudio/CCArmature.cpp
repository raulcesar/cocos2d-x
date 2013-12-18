/****************************************************************************
Copyright (c) 2013 cocos2d-x.org

http://www.cocos2d-x.org

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.
****************************************************************************/

#include "cocostudio/CCArmature.h"
#include "cocostudio/CCArmatureDataManager.h"
#include "cocostudio/CCArmatureDefine.h"
#include "cocostudio/CCDataReaderHelper.h"
#include "cocostudio/CCDatas.h"
#include "cocostudio/CCSkin.h"
#include "CCQuadCommand.h"
#include "CCRenderer.h"
#include "CCGroupCommand.h"

#if ENABLE_PHYSICS_BOX2D_DETECT
#include "Box2D/Box2D.h"
#elif ENABLE_PHYSICS_CHIPMUNK_DETECT
#include "chipmunk.h"
#endif

using namespace cocos2d;


namespace cocostudio {

Armature *Armature::create()
{
    Armature *armature = new Armature();
    if (armature && armature->init())
    {
        armature->autorelease();
        return armature;
    }
    CC_SAFE_DELETE(armature);
    return nullptr;
}


Armature *Armature::create(const char *name)
{
    Armature *armature = new Armature();
    if (armature && armature->init(name))
    {
        armature->autorelease();
        return armature;
    }
    CC_SAFE_DELETE(armature);
    return nullptr;
}

Armature *Armature::create(const char *name, Bone *parentBone)
{
    Armature *armature = new Armature();
    if (armature && armature->init(name, parentBone))
    {
        armature->autorelease();
        return armature;
    }
    CC_SAFE_DELETE(armature);
    return nullptr;
}

Armature::Armature()
    : _armatureData(nullptr)
    , _batchNode(nullptr)
    , _parentBone(nullptr)
    , _armatureTransformDirty(true)
    , _animation(nullptr)
{
}


Armature::~Armature(void)
{
    _boneDic.clear();
    _topBoneList.clear();

    CC_SAFE_DELETE(_animation);
}


bool Armature::init()
{
    return init(nullptr);
}


bool Armature::init(const char *name)
{
    bool bRet = false;
    do
    {
        removeAllChildren();

        CC_SAFE_DELETE(_animation);
        _animation = new ArmatureAnimation();
        _animation->init(this);

        _boneDic.clear();
        _topBoneList.clear();

        _blendFunc = BlendFunc::ALPHA_NON_PREMULTIPLIED;

        _name = name == nullptr ? "" : name;

        ArmatureDataManager *armatureDataManager = ArmatureDataManager::getInstance();

        if(_name.length() != 0)
        {
            _name = name;

            AnimationData *animationData = armatureDataManager->getAnimationData(name);
            CCASSERT(animationData, "AnimationData not exist! ");

            _animation->setAnimationData(animationData);


            ArmatureData *armatureData = armatureDataManager->getArmatureData(name);
            CCASSERT(armatureData, "");

            _armatureData = armatureData;

            for (auto& element : armatureData->boneDataDic)
            {
                Bone *bone = createBone(element.first.c_str());

                //! init bone's  Tween to 1st movement's 1st frame
                do
                {
                    MovementData *movData = animationData->getMovement(animationData->movementNames.at(0).c_str());
                    CC_BREAK_IF(!movData);

                    MovementBoneData *movBoneData = movData->getMovementBoneData(bone->getName().c_str());
                    CC_BREAK_IF(!movBoneData || movBoneData->frameList.size() <= 0);

                    FrameData *frameData = movBoneData->getFrameData(0);
                    CC_BREAK_IF(!frameData);

                    bone->getTweenData()->copy(frameData);
                    bone->changeDisplayByIndex(frameData->displayIndex, false);
                }
                while (0);
            }

            update(0);
            updateOffsetPoint();
        }
        else
        {
            _name = "new_armature";
            _armatureData = ArmatureData::create();
            _armatureData->name = _name;

            AnimationData *animationData = AnimationData::create();
            animationData->name = _name;

            armatureDataManager->addArmatureData(_name.c_str(), _armatureData);
            armatureDataManager->addAnimationData(_name.c_str(), animationData);

            _animation->setAnimationData(animationData);

        }

        setShaderProgram(ShaderCache::getInstance()->getProgram(GLProgram::SHADER_NAME_POSITION_TEXTURE_COLOR));

        unscheduleUpdate();
        scheduleUpdate();

        setCascadeOpacityEnabled(true);
        setCascadeColorEnabled(true);

        bRet = true;
    }
    while (0);

    return bRet;
}

bool Armature::init(const char *name, Bone *parentBone)
{
    _parentBone = parentBone;
    return init(name);
}


Bone *Armature::createBone(const char *boneName)
{
    Bone *existedBone = getBone(boneName);
    if(existedBone != nullptr)
        return existedBone;

    BoneData *boneData = (BoneData *)_armatureData->getBoneData(boneName);
    std::string parentName = boneData->parentName;

    Bone *bone = nullptr;

    if( parentName.length() != 0 )
    {
        createBone(parentName.c_str());
        bone = Bone::create(boneName);
        addBone(bone, parentName.c_str());
    }
    else
    {
        bone = Bone::create(boneName);
        addBone(bone, "");
    }

    bone->setBoneData(boneData);
    bone->getDisplayManager()->changeDisplayByIndex(-1, false);

    return bone;
}


void Armature::addBone(Bone *bone, const char *parentName)
{
    CCASSERT( bone != nullptr, "Argument must be non-nil");
    CCASSERT(_boneDic.at(bone->getName()) == nullptr, "bone already added. It can't be added again");

    if (nullptr != parentName)
    {
        Bone *boneParent = _boneDic.at(parentName);
        if (boneParent)
        {
            boneParent->addChildBone(bone);
        }
        else
        {
            _topBoneList.pushBack(bone);
        }
    }
    else
    {
        _topBoneList.pushBack(bone);
    }

    bone->setArmature(this);

    _boneDic.insert(bone->getName(), bone);
    addChild(bone);
}


void Armature::removeBone(Bone *bone, bool recursion)
{
    CCASSERT(bone != nullptr, "bone must be added to the bone dictionary!");

    bone->setArmature(nullptr);
    bone->removeFromParent(recursion);

    if (_topBoneList.contains(bone))
    {
        _topBoneList.eraseObject(bone);
    }
    _boneDic.erase(bone->getName());
    removeChild(bone, true);
}


Bone *Armature::getBone(const char *name) const
{
    return _boneDic.at(name);
}


void Armature::changeBoneParent(Bone *bone, const char *parentName)
{
    CCASSERT(bone != nullptr, "bone must be added to the bone dictionary!");

    if(bone->getParentBone())
    {
        bone->getParentBone()->getChildren().eraseObject(bone);
        bone->setParentBone(nullptr);
    }

    if (parentName != nullptr)
    {
        Bone *boneParent = _boneDic.at(parentName);

        if (boneParent)
        {
            boneParent->addChildBone(bone);
            if (_topBoneList.contains(bone))
            {
                _topBoneList.eraseObject(bone);
            }
        }
        else
        {
            _topBoneList.pushBack(bone);
        }
    }
}

const cocos2d::Map<std::string, Bone*>& Armature::getBoneDic() const
{
    return _boneDic;
}

const kmMat4& Armature::getNodeToParentTransform() const
{
    if (_transformDirty)
    {
        _armatureTransformDirty = true;

        // Translate values
        float x = _position.x;
        float y = _position.y;

        if (_ignoreAnchorPointForPosition)
        {
            x += _anchorPointInPoints.x;
            y += _anchorPointInPoints.y;
        }

        // Rotation values
        // Change rotation code to handle X and Y
        // If we skew with the exact same value for both x and y then we're simply just rotating
        float cx = 1, sx = 0, cy = 1, sy = 0;
        if (_rotationX || _rotationY)
        {
            float radiansX = -CC_DEGREES_TO_RADIANS(_rotationX);
            float radiansY = -CC_DEGREES_TO_RADIANS(_rotationY);
            cx = cosf(radiansX);
            sx = sinf(radiansX);
            cy = cosf(radiansY);
            sy = sinf(radiansY);
        }

        // Add offset point
        x += cy * _offsetPoint.x * _scaleX + -sx * _offsetPoint.y * _scaleY;
        y += sy * _offsetPoint.x * _scaleX + cx * _offsetPoint.y * _scaleY;

        bool needsSkewMatrix = ( _skewX || _skewY );

        // optimization:
        // inline anchor point calculation if skew is not needed
        // Adjusted transform calculation for rotational skew
        if (! needsSkewMatrix && !_anchorPointInPoints.equals(Point::ZERO))
        {
            x += cy * -_anchorPointInPoints.x * _scaleX + -sx * -_anchorPointInPoints.y * _scaleY;
            y += sy * -_anchorPointInPoints.x * _scaleX +  cx * -_anchorPointInPoints.y * _scaleY;
        }


        // Build Transform Matrix
        // Adjusted transform calculation for rotational skew
        kmScalar mat[] = { cy * _scaleX, sy * _scaleX,     0,  0,
                           -sx * _scaleY, cx * _scaleY,    0,  0,
                           0,  0,  1,  0,
                           x,  y,  0,  1 };

        kmMat4Fill(&_transform, mat);
        
        // XXX: Try to inline skew
        // If skew is needed, apply skew and then anchor point
        if (needsSkewMatrix)
        {
            kmMat4 skewMatrix = {    1, tanf(CC_DEGREES_TO_RADIANS(_skewY)), 0, 0,
                tanf(CC_DEGREES_TO_RADIANS(_skewX)),1, 0, 0,
                0,  0, 1, 0,
                0,  0,  0, 1};
            _transform = TransformConcat(skewMatrix, _transform);

            // adjust anchor point
            if (!_anchorPointInPoints.equals(Point::ZERO))
            {
                // XXX: Argh, kmMat needs a "translate" method
                _transform.mat[12] += -_anchorPointInPoints.x;
                _transform.mat[13] += -_anchorPointInPoints.y;
            }
        }

        if (_additionalTransformDirty)
        {
            _transform = TransformConcat(_transform, _additionalTransform);
            _additionalTransformDirty = false;
        }

        _transformDirty = false;
    }

    return _transform;
}

void Armature::updateOffsetPoint()
{
    // Set contentsize and Calculate anchor point.
    Rect rect = getBoundingBox();
    setContentSize(rect.size);
    _offsetPoint = Point(-rect.origin.x,  -rect.origin.y);
    if (rect.size.width != 0 && rect.size.height != 0)
    {
        setAnchorPoint(Point(_offsetPoint.x / rect.size.width, _offsetPoint.y / rect.size.height));
    }
}

void Armature::setAnimation(ArmatureAnimation *animation)
{
    _animation = animation;
}

ArmatureAnimation *Armature::getAnimation() const 
{
    return _animation;
}

bool Armature::getArmatureTransformDirty() const 
{
    return _armatureTransformDirty;
}

void Armature::update(float dt)
{
    _animation->update(dt);

    std::for_each(_topBoneList.begin(), _topBoneList.end(), [&dt](Bone* bone){
        bone->update(dt);
    });

    _armatureTransformDirty = false;
}

void Armature::draw()
{
    if (_parentBone == nullptr && _batchNode == nullptr)
    {
        CC_NODE_DRAW_SETUP();
    }


    for (auto& object : _children)
    {
        if (Bone *bone = dynamic_cast<Bone *>(object))
        {
            Node *node = bone->getDisplayRenderNode();

            if (nullptr == node)
                continue;

            switch (bone->getDisplayRenderNodeType())
            {
            case CS_DISPLAY_SPRITE:
            {
                Skin *skin = static_cast<Skin *>(node);
                skin->updateTransform();
                
                bool blendDirty = bone->isBlendDirty();
                
                if (blendDirty)
                {
                    skin->setBlendFunc(bone->getBlendFunc());
                }
                skin->draw();
            }
            break;
            case CS_DISPLAY_ARMATURE:
            {
                node->draw();
            }
            break;
            default:
            {
                node->visit();
                CC_NODE_DRAW_SETUP();
            }
            break;
            }
        }
        else if(Node *node = dynamic_cast<Node *>(object))
        {
            node->visit();
            CC_NODE_DRAW_SETUP();
        }
    }
}


void Armature::visit()
{
    // quick return if not visible. children won't be drawn.
    if (!_visible)
    {
        return;
    }
    kmGLPushMatrix();

    if (_grid && _grid->isActive())
    {
        _grid->beforeDraw();
    }

    transform();
    sortAllChildren();
    draw();

    // reset for next frame
    _orderOfArrival = 0;

    if (_grid && _grid->isActive())
    {
        _grid->afterDraw(this);
    }

    kmGLPopMatrix();
}

Rect Armature::getBoundingBox() const
{
    float minx, miny, maxx, maxy = 0;

    bool first = true;

    Rect boundingBox = Rect(0, 0, 0, 0);

    for_each(_children.begin(), _children.end(), [&minx, &miny, &maxx, &maxy, &first, &boundingBox](Node *object)
    {
        if (Bone *bone = dynamic_cast<Bone *>(object))
        {
            Rect r = bone->getDisplayManager()->getBoundingBox();

            if(first)
            {
                minx = r.getMinX();
                miny = r.getMinY();
                maxx = r.getMaxX();
                maxy = r.getMaxY();

                first = false;
            }
            else
            {
                minx = r.getMinX() < boundingBox.getMinX() ? r.getMinX() : boundingBox.getMinX();
                miny = r.getMinY() < boundingBox.getMinY() ? r.getMinY() : boundingBox.getMinY();
                maxx = r.getMaxX() > boundingBox.getMaxX() ? r.getMaxX() : boundingBox.getMaxX();
                maxy = r.getMaxY() > boundingBox.getMaxY() ? r.getMaxY() : boundingBox.getMaxY();
            }

            boundingBox.setRect(minx, miny, maxx - minx, maxy - miny);
        }

    });

    return RectApplyTransform(boundingBox, getNodeToParentTransform());
}

Bone *Armature::getBoneAtPoint(float x, float y) const 
{
    long length = _children.size();
    Bone *bs;

    for(long i = length - 1; i >= 0; i--)
    {
        bs = static_cast<Bone*>( _children.at(i) );
        if(bs->getDisplayManager()->containPoint(x, y))
        {
            return bs;
        }
    }
    return nullptr;
}


void Armature::setParentBone(Bone *parentBone)
{
    _parentBone = parentBone;

    for (auto& element : _boneDic)
    {
        element.second->setArmature(this);
    }
}

Bone *Armature::getParentBone() const
{
    return _parentBone;
}

#if ENABLE_PHYSICS_BOX2D_DETECT || ENABLE_PHYSICS_CHIPMUNK_DETECT

void CCArmature::setColliderFilter(ColliderFilter *filter)
{
    for (auto& element : _boneDic)
    {
        element.second->setColliderFilter(filter);
    }
}
#elif ENABLE_PHYSICS_SAVE_CALCULATED_VERTEX

void CCArmature::drawContour()
{
    for(auto& element : _boneDic)
    {
        Bone *bone = element.second;
        ColliderDetector *detector = bone->getColliderDetector();

        if (!detector)
            continue;

        const cocos2d::Vector<ColliderBody*>& bodyList = detector->getColliderBodyList();

        for (auto& object : bodyList)
        {
            ColliderBody *body = static_cast<ColliderBody*>(object);
            const std::vector<Point> &vertexList = body->getCalculatedVertexList();

            unsigned long length = vertexList.size();
            Point *points = new Point[length];
            for (unsigned long i = 0; i<length; i++)
            {
                Point p = vertexList.at(i);
                points[i].x = p.x;
                points[i].y = p.y;
            }
            DrawPrimitives::drawPoly( points, (unsigned int)length, true );

            delete points;
        }
    }
}

#endif

#if ENABLE_PHYSICS_BOX2D_DETECT
b2Body *Armature::getBody() const
{
    return _body;
}

void Armature::setBody(b2Body *body)
{
    if (_body == body)
    {
        return;
    }

    _body = body;
    _body->SetUserData(this);

    for(auto& object : _children)
    {
        if (Bone *bone = dynamic_cast<Bone *>(object))
        {
            auto displayList = bone->getDisplayManager()->getDecorativeDisplayList();

            for(auto displayObject : displayList)
            {
                ColliderDetector *detector = static_cast<DecorativeDisplay *>(displayObject)->getColliderDetector();
                if (detector != nullptr)
                {
                    detector->setBody(_body);
                }
            }
        }
    }
}

b2Fixture *Armature::getShapeList()
{
    if (_body)
    {
        return _body->GetFixtureList();
    }
    else
    {
        return nullptr;
    }
}

#elif ENABLE_PHYSICS_CHIPMUNK_DETECT
cpBody *Armature::getBody() const
{
    return _body;
}

void Armature::setBody(cpBody *body)
{
    if (_body == body)
    {
        return;
    }

    _body = body;
    _body->data = this;

    for (auto& object : _children)
    {
        if (Bone *bone = dynamic_cast<Bone *>(object))
        {
            auto displayList = bone->getDisplayManager()->getDecorativeDisplayList();

            for_each(displayList.begin(), displayList.end(), [&body](DecorativeDisplay* displayObject)
            {
                ColliderDetector *detector = displayObject->getColliderDetector();
                if (detector != nullptr)
                {
                    detector->setBody(body);
                }
            });
        }
    }
}

cpShape *Armature::getShapeList()
{
    if (_body)
    {
        return _body->shapeList_private;
    }
    else
    {
        return nullptr;
    }
}
#endif


}
