(* Prints the axioms that each theorem of the proof of tags 28 and 29 rests on. "Closed under the global context"
   means that the theorem rests on no axiom. *)
From MbeddedCbor Require ValueSharing Meaning Writer Keys.
Print Assumptions ValueSharing.read_item_extends.
Print Assumptions ValueSharing.read_item_fuel.
Print Assumptions ValueSharing.read_item_counted.
Print Assumptions ValueSharing.read_no_expansion.
Print Assumptions ValueSharing.read_item_sound.
Print Assumptions ValueSharing.read_no_dangling.
Print Assumptions ValueSharing.mark_names_its_item.
Print Assumptions ValueSharing.sharedref_names_entry.
Print Assumptions ValueSharing.read_item_linear.
Print Assumptions ValueSharing.read_linear.
Print Assumptions Meaning.read_unique.
Print Assumptions Meaning.read_no_missing.
Print Assumptions Meaning.read_meaning.
Print Assumptions Meaning.read_stream.
Print Assumptions Meaning.stream_read.
Print Assumptions Meaning.read_ok_iff.
Print Assumptions Writer.write_item_fuel.
Print Assumptions Writer.back_targets_fuel.
Print Assumptions Writer.preorder_fuel.
Print Assumptions Writer.marks_on_back_edges.
Print Assumptions Writer.write_size.
Print Assumptions Writer.write_read_back.
Print Assumptions Writer.read_closed_plain_tags.
Print Assumptions Writer.read_write_read.
Print Assumptions Writer.diamonds_size.
Print Assumptions Keys.same_key_correct.
