namespace Camel {
  [CCode (cheader_filename = "camel/camel.h", free_function = "camel_header_param_list_free", has_type_id = false)]
  [Compact]
	public class HeaderParam {
	}

	public abstract class Sasl : GLib.Object {
		public class weak Camel.ServiceAuthType? auth_type;
	}
}
