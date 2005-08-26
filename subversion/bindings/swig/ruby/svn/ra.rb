require "English"
require "svn/error"
require "svn/util"
require "svn/core"
require "svn/repos"
require "svn/ext/ra"

module Svn
  module Ra
    Util.set_constants(Ext::Ra, self)
    Util.set_methods(Ext::Ra, self)

    Session = SWIG::TYPE_p_svn_ra_session_t
  end
end
