#include "GameObject.h"

GameObject::GameObject()
{
	_info.mutable_posinfo()->set_x(0.f);
	_info.mutable_posinfo()->set_y(0.f);
	_info.mutable_posinfo()->set_z(0.f);
	_info.mutable_posinfo()->set_yaw(0.f);
}

GameObject::~GameObject()
{
}
