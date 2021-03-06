// This file is part of libigl, a simple c++ geometry processing library.
//
// Copyright (C) 2014 Daniele Panozzo <daniele.panozzo@gmail.com>IGL_VIEWER_WITH_NANOGUI_SERIALIZATION
//
// This Source Code Form is subject to the terms of the Mozilla Public License
// v. 2.0. If a copy of the MPL was not distributed with this file, You can
// obtain one at http://mozilla.org/MPL/2.0/.

#include "ViewerCore.h"
#include <igl/quat_to_mat.h>
#include <igl/snap_to_fixed_up.h>
#include <igl/look_at.h>
#include <igl/frustum.h>
#include <igl/ortho.h>
#include <igl/massmatrix.h>
#include <igl/barycenter.h>
#include <Eigen/Geometry>
#include <iostream>

IGL_INLINE void igl::viewer::ViewerCore::set_camera_position(
  const Eigen::Vector3f& pos)
{
  Eigen::Vector3f camera_direction = camera_center - camera_eye;
  camera_center = pos;
  camera_eye = camera_center - camera_direction;
}

IGL_INLINE void igl::viewer::ViewerCore::align_camera_center(
  const ViewerData& data)
{
  align_camera_center(data.V,data.F);
  camera_center += data.model_translation;
}

IGL_INLINE void igl::viewer::ViewerCore::align_camera_center(
  const Eigen::MatrixXd& V)
{
  align_camera_center(V,Eigen::MatrixXi());
}

IGL_INLINE void igl::viewer::ViewerCore::align_camera_center(
  const Eigen::MatrixXd& V,
  const Eigen::MatrixXi& F)
{
  if(V.rows() == 0)
    return;

  Eigen::Vector3f shift;
  get_zoom_and_shift_to_fit_mesh(V,F,camera_zoom,shift);
  Eigen::Vector3f camera_direction = camera_center - camera_eye;
  camera_center = -shift + global_translation;
  camera_eye = camera_center - camera_direction;
}

IGL_INLINE void igl::viewer::ViewerCore::get_zoom_and_shift_to_fit_mesh(
  const Eigen::MatrixXd& V,
  float& zoom,
  Eigen::Vector3f& shift)
{
  get_zoom_and_shift_to_fit_mesh(V,Eigen::MatrixXi(),zoom,shift);
}

IGL_INLINE void igl::viewer::ViewerCore::get_zoom_and_shift_to_fit_mesh(
  const Eigen::MatrixXd& V,
  const Eigen::MatrixXi& F,
  float& zoom,
  Eigen::Vector3f& shift)
{
  if (V.rows() == 0)
    return;

  Eigen::RowVector3d min_point;
  Eigen::RowVector3d max_point;
  Eigen::RowVector3d centroid;

  if(F.rows() == 0)
  {
    if(V.cols() == 3)
    {
      min_point = V.colwise().minCoeff();
      max_point = V.colwise().maxCoeff();
    } else if(V.cols() == 2)
    {
      min_point << V.colwise().minCoeff(),0;
      max_point << V.colwise().maxCoeff(),0;
    } else
      return;
  }
  else
  {
    Eigen::MatrixXd BC;
    if(F.rows() <= 1)
      BC = V;
    else
      igl::barycenter(V,F,BC);

    min_point = BC.colwise().minCoeff();
    max_point = BC.colwise().maxCoeff();
  }

  centroid = 0.5 * (min_point + max_point);
  shift = -centroid.cast<float>();
  double x_scale = fabs(max_point[0] - min_point[0]);
  double y_scale = fabs(max_point[1] - min_point[1]);
  double z_scale = fabs(max_point[2] - min_point[2]);
  zoom = std::max(z_scale,std::max(x_scale,y_scale)) / 2;
}

IGL_INLINE void igl::viewer::ViewerCore::clear_framebuffers()
{
  glClearColor(background_color[0],
               background_color[1],
               background_color[2],
               1.0f);
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
}

IGL_INLINE void igl::viewer::ViewerCore::draw(ViewerData& data, OpenGL_state& opengl, bool update_matrices)
{
  using namespace std;
  using namespace Eigen;

  if(!data.visible)
    return;

  if (data.depth_test)
    glEnable(GL_DEPTH_TEST);
  else
    glDisable(GL_DEPTH_TEST);

  /* Bind and potentially refresh mesh/line/point data */
  if (data.dirty)
  {
    opengl.set_data(data,data.invert_normals);
    data.dirty = ViewerData::DIRTY_NONE;
  }
  opengl.bind_mesh();

  // Initialize uniform
  glViewport(viewport(0), viewport(1), viewport(2), viewport(3));

  if(update_matrices)
  {
    //model = Eigen::Matrix4f::Identity();
    view  = Eigen::Matrix4f::Identity();
    proj  = Eigen::Matrix4f::Identity();

    // camera zoom by shifting
    Vector3f camera_eye_zoomed = camera_center + (camera_eye-camera_center)*camera_zoom;
    float camera_dnear_zoomed = camera_dnear * camera_zoom;
    float camera_dfar_zoomed = camera_dfar * camera_zoom;

    // Set view
    look_at(camera_eye_zoomed,camera_center, camera_up, view);

    float width  = viewport(2);
    float height = viewport(3);

    // Set projection
    if (orthographic)
    {
      float length = (camera_eye_zoomed - camera_center).norm();
      float h = tan(camera_view_angle/360.0 * M_PI) * (length);
      // real camera Zoom
      //ortho(-h*width/height*camera_zoom, h*width/height*camera_zoom, -h*camera_zoom, h*camera_zoom, camera_dnear, camera_dfar,proj); 
      ortho(-h*width/height,h*width/height,-h,h,camera_dnear_zoomed,camera_dfar_zoomed,proj);
    }
    else
    {
      float fH = tan(camera_view_angle / 360.0 * M_PI) * camera_dnear_zoomed;
      float fW = fH * (double)width/(double)height;
      //frustum(-fW*camera_zoom, fW*camera_zoom, -fH*camera_zoom, fH*camera_zoom, camera_dnear, camera_dfar,proj); // real camera Zoom
      frustum(-fW,fW,-fH,fH,camera_dnear_zoomed,camera_dfar_zoomed,proj);
    }
    // end projection

    // Set model transformation
    Eigen::Matrix4f GR = Eigen::Matrix4f::Identity();
    Eigen::Matrix4f S = Eigen::Matrix4f::Identity();
    Eigen::Matrix4f SI = Eigen::Matrix4f::Identity();
    Eigen::Matrix4f T = Eigen::Matrix4f::Identity();
    Eigen::Matrix4f GT = Eigen::Matrix4f::Identity();

    float mat[16];
    igl::quat_to_mat(trackball_angle.coeffs().data(),mat);

    for(unsigned i=0;i<4;++i)
      for(unsigned j=0;j<4;++j)
        GR(i,j) = mat[i+4*j];
    
    S.col(3).head(3) = -camera_center;
    SI.col(3).head(3) = camera_center;
    T.col(3).head(3) = data.model_translation;
    GT.col(3).head(3) = global_translation;

    data.model = SI * GR * S * GT * T;

    //model.col(3).head(3) += model.topLeftCorner(3,3)*-camera_center;// data.model_translation;
  }

  // Send transformations to the GPU
  GLint modeli = opengl.shader_mesh.uniform("model");
  GLint viewi  = opengl.shader_mesh.uniform("view");
  GLint proji  = opengl.shader_mesh.uniform("proj");
  glUniformMatrix4fv(modeli, 1, GL_FALSE, data.model.data());
  glUniformMatrix4fv(viewi, 1, GL_FALSE, view.data());
  glUniformMatrix4fv(proji, 1, GL_FALSE, proj.data());

  // Light parameters
  GLint specular_exponenti    = opengl.shader_mesh.uniform("specular_exponent");
  GLint light_position_worldi = opengl.shader_mesh.uniform("light_position_world");
  GLint lighting_factori      = opengl.shader_mesh.uniform("lighting_factor");
  GLint fixed_colori          = opengl.shader_mesh.uniform("fixed_color");
  GLint texture_factori       = opengl.shader_mesh.uniform("texture_factor");

  glUniform1f(specular_exponenti,shininess);
  Vector3f rev_light = -1.*light_position;
  glUniform3fv(light_position_worldi, 1, rev_light.data());
  glUniform1f(lighting_factori, lighting_factor); // enables lighting
  glUniform4f(fixed_colori, 0.0, 0.0, 0.0, 0.0);

  if (data.V.rows()>0)
  {
    // Render fill
    if (data.show_faces)
    {
      // Texture
      glUniform1f(texture_factori,data.show_texture ? 1.0f : 0.0f);
      opengl.draw_mesh(true);
      glUniform1f(texture_factori, 0.0f);
    }

    // Render wireframe
    if (data.show_lines)
    {
      glLineWidth(data.line_width);
      glUniform4f(fixed_colori,line_color[0],line_color[1],
                  line_color[2], 1.0f);
      opengl.draw_mesh(false);
      glUniform4f(fixed_colori, 0.0f, 0.0f, 0.0f, 0.0f);
    }

#ifdef IGL_VIEWER_WITH_NANOGUI
    if (data.show_vertid)
    {
      textrenderer.BeginDraw(view*data.model, proj, viewport, data.object_scale);
      for (int i=0; i<data.V.rows(); ++i)
        textrenderer.DrawText(data.V.row(i),data.V_normals.row(i),to_string(i));
      textrenderer.EndDraw();
    }

    if (data.show_faceid)
    {
      textrenderer.BeginDraw(view*data.model, proj, viewport, data.object_scale);

      for (int i=0; i<data.F.rows(); ++i)
      {
        Eigen::RowVector3d p = Eigen::RowVector3d::Zero();
        for (int j=0;j<data.F.cols();++j)
          p += data.V.row(data.F(i,j));
        p /= data.F.cols();

        textrenderer.DrawText(p, data.F_normals.row(i), to_string(i));
      }
      textrenderer.EndDraw();
    }
#endif
  }

  if (data.show_overlay)
  {
    if (data.show_overlay_depth)
      glEnable(GL_DEPTH_TEST);
    else
      glDisable(GL_DEPTH_TEST);

    if (data.lines.rows() > 0)
    {
      opengl.bind_overlay_lines();
      modeli = opengl.shader_overlay_lines.uniform("model");
      viewi  = opengl.shader_overlay_lines.uniform("view");
      proji  = opengl.shader_overlay_lines.uniform("proj");

      glUniformMatrix4fv(modeli, 1, GL_FALSE, data.model.data());
      glUniformMatrix4fv(viewi, 1, GL_FALSE, view.data());
      glUniformMatrix4fv(proji, 1, GL_FALSE, proj.data());
      // This must be enabled, otherwise glLineWidth has no effect
      glEnable(GL_LINE_SMOOTH);
      glLineWidth(overlay_line_width);
      opengl.draw_overlay_lines();
			glLineWidth(data.line_width);
    }

    if (data.points.rows() > 0)
    {
      opengl.bind_overlay_points();
      modeli = opengl.shader_overlay_points.uniform("model");
      viewi  = opengl.shader_overlay_points.uniform("view");
      proji  = opengl.shader_overlay_points.uniform("proj");

      glUniformMatrix4fv(modeli, 1, GL_FALSE, data.model.data());
      glUniformMatrix4fv(viewi, 1, GL_FALSE, view.data());
      glUniformMatrix4fv(proji, 1, GL_FALSE, proj.data());
      glPointSize(data.point_size);

      opengl.draw_overlay_points();
    }

#ifdef IGL_VIEWER_WITH_NANOGUI
    if (data.labels_positions.rows() > 0)
    {
      textrenderer.BeginDraw(view*data.model, proj, viewport, data.object_scale);
      for (int i=0; i<data.labels_positions.rows(); ++i)
        textrenderer.DrawText(data.labels_positions.row(i), Eigen::Vector3d(0.0,0.0,0.0),
            data.labels_strings[i],data.labels_colors.row(i));
      textrenderer.EndDraw();
    }
#endif

    glEnable(GL_DEPTH_TEST);
  }

}

IGL_INLINE void igl::viewer::ViewerCore::draw_buffer(ViewerData& data,
                                                     OpenGL_state& opengl,
                                                     bool update_matrices,
                                                     Eigen::Matrix<unsigned char,Eigen::Dynamic,Eigen::Dynamic>& R,
                                                     Eigen::Matrix<unsigned char,Eigen::Dynamic,Eigen::Dynamic>& G,
                                                     Eigen::Matrix<unsigned char,Eigen::Dynamic,Eigen::Dynamic>& B,
                                                     Eigen::Matrix<unsigned char,Eigen::Dynamic,Eigen::Dynamic>& A)
{
  std::vector<ViewerData*> dataBuffer;
  dataBuffer.push_back(&data);
  std::vector<OpenGL_state*> openglBuffer;
  openglBuffer.push_back(&opengl);

  draw_buffer(dataBuffer,openglBuffer,update_matrices,R,G,B,A);
}

IGL_INLINE void igl::viewer::ViewerCore::draw_buffer(std::vector<ViewerData*>& data,
                                                      std::vector<OpenGL_state*>& opengl,
                                                      bool update_matrices,
                                                      Eigen::Matrix<unsigned char,Eigen::Dynamic,Eigen::Dynamic>& R,
                                                      Eigen::Matrix<unsigned char,Eigen::Dynamic,Eigen::Dynamic>& G,
                                                      Eigen::Matrix<unsigned char,Eigen::Dynamic,Eigen::Dynamic>& B,
                                                      Eigen::Matrix<unsigned char,Eigen::Dynamic,Eigen::Dynamic>& A)
{
  assert(R.rows() == G.rows() && G.rows() == B.rows() && B.rows() == A.rows());
  assert(R.cols() == G.cols() && G.cols() == B.cols() && B.cols() == A.cols());

  int x = R.rows();
  int y = R.cols();

  // Create frame buffer
  GLuint frameBuffer;
  glGenFramebuffers(1, &frameBuffer);
  glBindFramebuffer(GL_FRAMEBUFFER, frameBuffer);

  // Create texture to hold color buffer
  GLuint texColorBuffer;
  glGenTextures(1, &texColorBuffer);
  glBindTexture(GL_TEXTURE_2D, texColorBuffer);

  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, x, y, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);

  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texColorBuffer, 0);

  // Create Renderbuffer Object to hold depth and stencil buffers
  GLuint rboDepthStencil;
  glGenRenderbuffers(1, &rboDepthStencil);
  glBindRenderbuffer(GL_RENDERBUFFER, rboDepthStencil);
  glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, x, y);
  glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, rboDepthStencil);

  assert(glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE);

  glBindFramebuffer(GL_FRAMEBUFFER, frameBuffer);

  // Clear the buffer
  glClearColor(background_color(0),background_color(1),background_color(2),0.f);
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

  // Save old viewport
  Eigen::Vector4f viewport_ori = viewport;
  viewport << 0,0,x,y;

  // Draw
  for(int i=0;i<data.size();i++)
  {
    draw(*data[i],*opengl[i],update_matrices);
  }

  // Restore viewport
  viewport = viewport_ori;

  // Copy back in the given Eigen matrices
  GLubyte* pixels = (GLubyte*)calloc(x*y*4,sizeof(GLubyte));
  glReadPixels
  (
   0, 0,
   x, y,
   GL_RGBA, GL_UNSIGNED_BYTE, pixels
   );

  int count = 0;
  for (unsigned j=0; j<y; ++j)
  {
    for (unsigned i=0; i<x; ++i)
    {
      R(i,j) = pixels[count*4+0];
      G(i,j) = pixels[count*4+1];
      B(i,j) = pixels[count*4+2];
      A(i,j) = pixels[count*4+3];
      ++count;
    }
  }

  // Clean up
  free(pixels);
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
  glDeleteRenderbuffers(1, &rboDepthStencil);
  glDeleteTextures(1, &texColorBuffer);
  glDeleteFramebuffers(1, &frameBuffer);
}

IGL_INLINE void igl::viewer::ViewerCore::set_rotation_type(
  const igl::viewer::ViewerCore::RotationType & value)
{
  using namespace Eigen;
  using namespace std;
  const RotationType old_rotation_type = rotation_type;
  rotation_type = value;
  if(rotation_type == ROTATION_TYPE_TWO_AXIS_VALUATOR_FIXED_UP &&
    old_rotation_type != ROTATION_TYPE_TWO_AXIS_VALUATOR_FIXED_UP)
  {
    snap_to_fixed_up(Quaternionf(trackball_angle),trackball_angle);
  }
}


IGL_INLINE igl::viewer::ViewerCore::ViewerCore()
{
  // Default colors
  background_color << 0.3f, 0.3f, 0.5f, 1.0f;
  line_color << 0.0f,0.0f,0.0f,1.0f;

  // Default lights settings
  shininess = 35.0f;
  light_position << 0.0f, -0.30f, -5000.0f;
  lighting_factor = 1.0f; //on

  // Global scene transformation
  trackball_angle = Eigen::Quaternionf::Identity();
  set_rotation_type(ViewerCore::ROTATION_TYPE_TRACKBALL);
  global_translation << 0,0,0;

  // Camera parameters
  camera_zoom = 1.0f;
  orthographic = false;
  camera_view_angle = 45.0;
  camera_dnear = 0.0;
  camera_dfar = 100.0;
  camera_eye << 0,0,5;
  camera_center << 0,0,0;
  camera_up << 0,1,0;

  is_animating = false;
  animation_max_fps = 30.;
}

IGL_INLINE void igl::viewer::ViewerCore::init()
{
#ifdef IGL_VIEWER_WITH_NANOGUI
  textrenderer.Init();
#endif
}

IGL_INLINE void igl::viewer::ViewerCore::shut()
{
#ifdef IGL_VIEWER_WITH_NANOGUI
  textrenderer.Shut();
#endif
}
