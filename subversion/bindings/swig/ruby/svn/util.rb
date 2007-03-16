if /cygwin|mingw|mswin32|bccwin32/.match(RUBY_PLATFORM)
  $LOAD_PATH.each do |load_path|
    svn_ext_path = File.join(load_path, "svn", "ext")
    if File.exists?(svn_ext_path)
      svn_ext_path_win = File.expand_path(svn_ext_path)
      svn_ext_path_win = svn_ext_path.gsub(File::SEPARATOR, File::ALT_SEPARATOR)
      unless ENV["PATH"].split(";").find {|path| path == svn_ext_path_win}
        ENV["PATH"] = "#{svn_ext_path_win};#{ENV['PATH']}"
      end
    end
  end
end

require 'tempfile'

unless respond_to?(:funcall)
  module Kernel
    alias funcall __send__
  end
end

module Svn
  module Util

    module_function
    def to_ruby_class_name(name)
      name.split("_").collect do |x|
        "#{x[0,1].upcase}#{x[1..-1].downcase}"
      end.join("")
    end

    def to_ruby_const_name(name)
      name.upcase
    end

    def valid_rev?(rev)
      rev and rev >= 0
    end

    def copy?(copyfrom_path, copyfrom_rev)
      Util.valid_rev?(copyfrom_rev) && !copyfrom_path.nil?
    end

    def set_constants(ext_mod, target_mod=self)
      target_name = nil
      ext_mod.constants.each do |const|
        target_name = nil
        case const
        when /^SVN__/
          # ignore private constants
        when /^SVN_(?:#{target_mod.name.split("::").last.upcase}_)?/
          target_name = $POSTMATCH
        when /^SWIG_SVN_/
          target_name = $POSTMATCH
        when /^Svn_(?:#{target_mod.name.split("::").last.downcase}_)?(.+)_t$/
          target_name = to_ruby_class_name($1)
        when /^Svn_(?:#{target_mod.name.split("::").last.downcase}_)?/
          target_name = to_ruby_const_name($POSTMATCH)
#         else
#           puts const
        end
        unless target_name.nil?
          target_mod.const_set(target_name, ext_mod.const_get(const))
        end
      end
    end

    def set_methods(ext_mod, target_mod=self)
      target_name = nil
      ext_mod.public_methods(false).each do |meth|
        target_name = nil
        case meth
        when /^svn_(?:#{target_mod.name.split("::").last.downcase}_)?/
          target_name = $POSTMATCH
        when /^apr_/
          target_name = meth
        end
        unless target_name.nil?
          target_mod.module_eval(<<-EOC, __FILE__, __LINE__ + 1)
            def #{target_name}(*args, &block)
              #{ext_mod.name}.#{meth}(*args, &block)
            end
            module_function :#{target_name}
EOC
        end
      end
    end

    def filename_to_temp_file(filename)
      file = Tempfile.new("svn-ruby")
      file.binmode
      file.print(File.read(filename))
      file.close
      file.open
      file.binmode
      file
    end
  end
end
