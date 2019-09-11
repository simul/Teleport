//#version 310 es
precision highp float;

//From Application VIA
layout(location = 0) in vec3 a_Position;
layout(location = 1) in vec3 a_Normal;
layout(location = 2) in vec4 a_Tangent;
layout(location = 3) in vec2 a_UV0;
layout(location = 4) in vec2 a_UV1;
layout(location = 5) in vec4 a_Color;
layout(location = 6) in vec4 a_Joint;
layout(location = 7) in vec4 a_Weights;

//From Application SR
layout(std140, binding = 0) uniform CameraUB
{
    mat4 u_ProjectionMatrix;
    mat4 u_ViewMatrix;
    vec4 u_Orientation; //Quaternion
    vec3 u_Position;
    float _pad;
}cam;

layout(std140, binding = 1) uniform TransformUB
{
    mat4 u_ModelMatrix;
}model;

//To Fragment Varying
layout(location = 0)  out vec3 v_Position;
layout(location = 1)  out vec3 v_Normal;
layout(location = 2)  out vec3 v_Tangent;
layout(location = 3)  out vec3 v_Binormal;
layout(location = 4)  out mat3 v_TBN;
layout(location = 7)  out vec2 v_UV0;
layout(location = 8)  out vec2 v_UV1;
layout(location = 9)  out vec4 v_Color;
layout(location = 10) out vec4 v_Joint;
layout(location = 11) out vec4 v_Weights;
layout(location = 12) out vec3 v_CameraPosition;

void main()
{
    gl_Position = cam.u_ProjectionMatrix * cam.u_ViewMatrix * model.u_ModelMatrix * vec4(a_Position, 1.0);

    v_Position 	= mat3(model.u_ModelMatrix) * a_Position;
    v_Normal	= normalize(mat3(model.u_ModelMatrix) * a_Normal);
    v_Tangent	= normalize(mat3(model.u_ModelMatrix) * a_Tangent.xyz);
    v_Binormal	= normalize(cross(v_Normal, v_Tangent));
    v_TBN		= mat3(v_Tangent, v_Binormal, v_Normal);
    v_UV0		= a_UV0;
    v_UV1		= a_UV1;
    v_Color		= a_Color;
    v_Joint		= a_Joint;
    v_Weights	= a_Weights;
    v_CameraPosition = cam.u_Position;
}