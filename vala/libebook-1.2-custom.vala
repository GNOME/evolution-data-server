/* Custom vapi needed because bgo#666797 prevents generics being exposed in the .metadata file. */
namespace E {
	[CCode (type_id = "e_contact_get_type ()")]
	public class Contact : E.VCard {
		[CCode (simple_generics = true)]
		public T? @get<T> (E.ContactField field_id);
		[CCode (simple_generics = true)]
		public unowned T? get_const<T> (E.ContactField field_id);
		[CCode (simple_generics = true)]
		public void @set<T> (E.ContactField field_id, T value);
	}
}
