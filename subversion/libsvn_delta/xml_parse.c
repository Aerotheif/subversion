/*
 * deltaparse.c : create an svn_delta_t from an XML stream
 *
 * ================================================================
 * Copyright (c) 2000 Collab.Net.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 * 1. Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the distribution.
 *
 * 3. The end-user documentation included with the redistribution, if
 * any, must include the following acknowlegement: "This product includes
 * software developed by Collab.Net (http://www.Collab.Net/)."
 * Alternately, this acknowlegement may appear in the software itself, if
 * and wherever such third-party acknowlegements normally appear.
 *
 * 4. The hosted project names must not be used to endorse or promote
 * products derived from this software without prior written
 * permission. For written permission, please contact info@collab.net.
 *
 * 5. Products derived from this software may not use the "Tigris" name
 * nor may "Tigris" appear in their names without prior written
 * permission of Collab.Net.
 *
 * THIS SOFTWARE IS PROVIDED ``AS IS'' AND ANY EXPRESSED OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL COLLAB.NET OR ITS CONTRIBUTORS BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE
 * GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER
 * IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * ====================================================================
 *
 * This software consists of voluntary contributions made by many
 * individuals on behalf of Collab.Net.
 */

/* ==================================================================== */


/*
  This library contains callbacks to use with the "expat-lite" XML
  parser.  The callbacks produce an svn_delta_t structure from a
  stream containing Subversion's XML delta representation.

  To use this library, see "deltaparse-test.c" in tests/.

  Essentially, one must

  * create an XML_Parser
  * register the callbacks (below) with the parser
  * call XML_Parse() on a bytestream

*/


#include <stdio.h>
#include <string.h>
#include "svn_types.h"
#include "svn_string.h"
#include "xmlparse.h"



/* Callback:  called whenever we find a new tag (open paren).

    The *name argument contains the name of the tag,
    and the **atts list is a dumb list of name/value pairs, all
    null-terminated Cstrings, and ending with an extra final NULL.

*/

void svn_xml_startElement(void *userData, const char *name, const char **atts)
{
  int i;
  char *attr_name, *attr_value;
  svn_delta_digger_t *my_digger = (svn_delta_digger_t *) userData;

  if (strcmp (name, "tree-delta"))
    {
      /* Found a new tree-delta element */
      /* Create new svn_delta_t structure here, using **atts */
    }

  else if (strcmp (name, "text-delta"))
    {
      /* Found a new text-delta element */
      /* Please mark flag in edit_content structure (should be the
         last structure on our growing delta) */
    }

  else if (strcmp (name, "prop-delta"))
    {
      /* Found a new prop-delta element */
      /* Please mark flag in edit_content structure (should be the
         last structure on our growing delta) */
    }

  else if (strcmp (name, "new"))
    {
      /* Found a new svn_edit_t */
      /* Build a new edit struct, fill with **attrs */
    }

  else if (strcmp (name, "replace"))
    {
      /* Found a new svn_edit_t */
      /* Build a new edit struct, fill with **attrs */
    }

  else if (strcmp (name, "delete"))
    {
      /* Found a new svn_edit_t */
      /* Build a new edit struct, fill with **attrs */
    }

  else if (strcmp (name, "file"))
    {
      /* Found a new svn_edit_content_t */
      /* Build a edit_content_t */
      /* Build an ancestor out of **attrs */
    }

  else if (strcmp (name, "dir"))
    {
      /* Found a new svn_edit_content_t */
      /* Build a edit_content_t */
      /* Build an ancestor out of **attrs */
      /* call (*dir_handler) (svn_delta_digger_t *digger,
                              svn_ancestor_t *ancestor); */
    }

  else
    {
      /* Found some other random tag
         -- PUNT to the *caller's* default handler! */


    }



  /* Read all attribute name/value pairs */
  while (*atts)
    {
      printf ("(name=%s ", *atts++);
      printf ("value=%s)", *atts++);
    }
  printf ("\n");

}



/*  Callback:  called whenever we find a close tag (close paren) */

void svn_xml_endElement(void *userData, const char *name)
{
  svn_delta_digger_t *my_digger = (svn_delta_digger_t *) userData;


}



/* Callback: called whenever we find data within a tag.
   (Of course, we only care about data within the "text-delta" tag.)  */

void svn_xml_DataHandler(void *userData, const char *data, int len)
{
  svn_delta_digger_t *my_digger = (svn_delta_digger_t *) userData;

  /* TODO: Check context of my_digger->delta, make sure that *data is
     relevant before we bother our data_handler() */

  (* (my_digger->data_handler)) (my_digger, data, len);

}





