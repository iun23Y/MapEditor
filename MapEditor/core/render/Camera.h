#pragma once

#include <SFML/Graphics.hpp>
#include <glm/glm.hpp>

class Camera
{
public:
    enum class Mode { Orthographic, Perspective };

    Camera();

    void setTarget(const glm::vec3& target);
    void setOrbit(float yaw, float pitch, float dist);
    void addOrbit(float dYaw, float dPitch);
    void addZoom(float delta);
    void pan(float dx, float dy);

    void setMode(Mode m) { mode = m; }
    void setAspect(float aspect) { this->aspect = aspect; }
    void setFovDegrees(float fov) { this->fov = fov; }
    void setOrthoSize(float size) { orthoSize = size; }

    glm::vec3 getPosition() const;
    glm::mat4 getView() const;
    glm::mat4 getProjection() const;
    glm::mat4 getViewProjection() const { return getProjection() * getView(); }

    void screenToRay(sf::Vector2i screenPos, sf::Vector2u screenSize,
        glm::vec3& outOrigin, glm::vec3& outDirection) const;

private:
    glm::vec3 target{ 0.f };
    float yaw = 45.f;
    float pitch = -30.f;
    float distance = 100.f;

    Mode mode = Mode::Perspective;
    float aspect = 16.f / 9.f;
    float fov = 60.f;
    float orthoSize = 100.f;
    float nearPlane = 0.1f;
    float farPlane = 10000.f;
};