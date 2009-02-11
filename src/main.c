/* This file is part of pongo
 *
 * AUTHORS
 *     Sven Herzberg  <herzi@lanedo.com>
 *
 * Copyright (C) 2009  Sven Herzberg
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public License as
 * published by the Free Software Foundation; either version 2.1 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307
 * USA
 */

#include <gst/gst.h>

#include <glib/gi18n.h>

static GMainLoop * loop = NULL;

static void
sigint_handler (int signal)
{
  g_main_loop_quit (loop);
}

int
main (int   argc,
      char**argv)
{
  GstElement* mix;
  GstElement* pipeline;
  GstPad    * cam_pad;
  gchar     * launch;

  gchar const* out_file = NULL;
  gint         out_w    =   -1;
  gint         out_h    =   -1;
  gint         cam_w    =   -1;
  gint         cam_h    =   -1;
  gdouble      cam_opacity = -0.1;
  gchar const* v4l_device = NULL;

  GOptionEntry    entries[] = {
    {"output", 'o', 0, G_OPTION_ARG_FILENAME, &out_file, N_("output file (default: final.ogv)"), N_("FILE")},
    {"width",  'w', 0, G_OPTION_ARG_INT, &out_w, N_("output width (default: 1024)"), N_("WIDTH")},
    {"height", 'h', 0, G_OPTION_ARG_INT, &out_h, N_("output height (default: 768)"), N_("HEIGHT")},
    {"cam-width", 'W', 0, G_OPTION_ARG_INT, &cam_w, N_("camera width (default: 300)"), N_("WIDTH")},
    {"cam-height", 'H', 0, G_OPTION_ARG_INT, &cam_h, N_("camera height (default: 200)"), N_("HEIGHT")},
    {"opacity", 'O', 0, G_OPTION_ARG_DOUBLE, &cam_opacity, N_("camera opacity (default 0.8)"), N_("OPACITY")},
    {"device", 'd', 0, G_OPTION_ARG_FILENAME, &v4l_device, N_("camera device (default: /dev/video0)"), N_("DEVICE")},
    {NULL}
  };

  GOptionContext* options;
  GError        * error = NULL;

  g_thread_init (NULL);

  options = g_option_context_new ("");
  g_option_context_add_main_entries (options, entries, NULL);
  g_option_context_add_group (options, gst_init_get_option_group ());
  if (!g_option_context_parse (options, &argc, &argv, &error))
    {
      gchar* help = NULL;
#if GLIB_CHECK_VERSION(2,14,0)
      help = g_option_context_get_help (options, TRUE, NULL);
#endif
      g_printerr ("%s%s", help ? help : "", error ? error->message : "");
      g_free (help);
      g_error_free (error);
      return 1;
    }

  g_option_context_free (options);

  if (!out_file)
    {
      out_file = g_strdup ("final.ogv");
    }
  if (out_w == -1)
    {
      out_w = 1024;
    }
  if (out_h == -1)
    {
      out_h = 768;
    }
  if (cam_w == -1)
    {
      cam_w = 300;
    }
  if (cam_h == -1)
    {
      cam_h = 200;
    }
  if (cam_opacity < 0.0)
    {
      cam_opacity = 0.8;
    }
  if (!v4l_device)
    {
      v4l_device = "/dev/video0";
    }

  signal (SIGINT, sigint_handler);

  launch = g_strdup_printf ("videomixer name=mix ! ffmpegcolorspace ! queue ! theoraenc ! oggmux name=mux ! filesink location=%s "
                            "istximagesrc name=xsrc use-damage=false ! videorate ! video/x-raw-rgb,framerate=10/1 ! ffmpegcolorspace ! videoscale method=1 ! video/x-raw-yuv,width=%d,height=%d ! mix.sink_0 "
                            "v4l2src name=camsrc device=%s ! ffmpegcolorspace ! videorate ! video/x-raw-yuv,framerate=10/1,width=320,height=240 ! queue ! videoscale ! ffmpegcolorspace ! video/x-raw-yuv,height=%d,height=%d ! mix.sink_1 "
                            "alsasrc name=asrc ! queue ! audioconvert ! vorbisenc ! mux."
                            ,
                            out_file, out_w, out_h, v4l_device, cam_w, cam_h);

  pipeline = gst_parse_launch (launch, &error);

  if (error)
    {
      g_warning ("Couldn't construct pipeline: %s",
                 error->message);
      g_error_free (error);
      return 1;
    }

  mix = gst_bin_get_by_name (GST_BIN (pipeline), "mix");

  cam_pad = gst_element_get_pad (mix, "sink_1");
  g_object_set (cam_pad,
                "xpos", out_w-cam_w,
                "ypos", out_h-cam_h,
                "alpha", cam_opacity,
                "zorder", 1,
                NULL);

  gst_element_set_state (pipeline, GST_STATE_PLAYING);
  g_print ("Capturing...\n");

  loop = g_main_loop_new (NULL, FALSE);
  g_main_loop_run (loop);

  g_print ("Stopping capture...\n");
  {
    GstEvent* eos = gst_event_new_eos ();
    gst_element_send_event (gst_bin_get_by_name (GST_BIN (pipeline), "camsrc"),
                            eos);

    eos = gst_event_new_eos ();
    gst_element_send_event (gst_bin_get_by_name (GST_BIN (pipeline), "xsrc"),
                            eos);

    eos = gst_event_new_eos ();
    gst_element_send_event (gst_bin_get_by_name (GST_BIN (pipeline), "asrc"),
                            eos);

    gst_bus_poll (gst_pipeline_get_bus (GST_PIPELINE (pipeline)),
                  GST_MESSAGE_EOS | GST_MESSAGE_ERROR,
                  -1);
  }
  gst_element_set_state (pipeline, GST_STATE_NULL);
  g_print ("Done.\n");

  return 0;
}

