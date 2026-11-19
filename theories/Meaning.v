(* The meaning of the graph that read builds, http://cbor.schmorp.de/value-sharing: "Implementations that do not
   support sharing can duplicate the values after decoding". Each depth-k unfolding of the graph equals the depth-k
   reading of the stream by a reader that builds no graph and duplicates each shared value. *)

From Stdlib Require Import List Arith Lia PeanoNat.
Import ListNotations.
From MbeddedCbor Require Import ValueSharing.

(* A value, cut at a depth. Cut stands where the depth is used up. No proof needs an induction scheme of it. *)
Unset Elimination Schemes.
Inductive tree :=
| Cut
| TU (n : nat)
| TL (n : nat)
| TA (children : list tree)
| TM (entries : list (tree * tree))
| TT (t : nat) (content : tree).
Set Elimination Schemes.

(* The first node with the id in the graph. *)
Fixpoint find (g : list (nat * node)) (id : nat) : option node :=
  match g with
  | [] => None
  | (i, n) :: g' => if Nat.eqb i id then Some n else find g' id
  end.

(* The node of the id, its children unfolded to one depth less. An id that is not in the graph gives Cut;
   read_no_missing shows that a successful read has no such id. *)
Fixpoint unfold (g : list (nat * node)) (k : nat) (id : nat) : tree :=
  match k with
  | 0 => Cut
  | S k' =>
    match find g id with
    | None => Cut
    | Some (NUInt n) => TU n
    | Some (NLeaf n) => TL n
    | Some (NArr ids) => TA (map (unfold g k') ids)
    | Some (NMap ps) => TM (map (fun p => (unfold g k' (fst p), unfold g k' (snd p))) ps)
    | Some (NTag t c) => TT t (unfold g k' c)
    end
  end.

Fixpoint tree_pairs (l : list tree) : list (tree * tree) :=
  match l with
  | a :: b :: l' => (a, b) :: tree_pairs l'
  | _ => []
  end.

(* The stream after a chain of tags 28. It does not use the chain of the reader. *)
Fixpoint strip (ts : list tok) : list tok :=
  match ts with
  | TTag t :: r => if Nat.eqb t shareable then strip r else ts
  | _ => ts
  end.
Arguments strip : simpl never.

(* Entry n: the item that the n-th tag 28 of the stream marks, after the chain of tags 28 that it starts. *)
Fixpoint marks (ts : list tok) : list (list tok) :=
  match ts with
  | [] => []
  | TTag t :: r => if Nat.eqb t shareable then strip r :: marks r else marks r
  | _ :: r => marks r
  end.

(* skips skips n items one after the other. *)
Fixpoint skips (sk : list tok -> option (list tok)) (n : nat) (ts : list tok) : option (list tok) :=
  match n with
  | 0 => Some ts
  | S n' =>
    match sk ts with
    | Some r => skips sk n' r
    | None => None
    end
  end.

(* skip gives the stream after one item. A tag 29 is its head and its index. *)
Fixpoint skip (fuel : nat) (ts : list tok) : option (list tok) :=
  match fuel with
  | 0 => None
  | S f =>
    match ts with
    | [] => None
    | TUInt _ :: r => Some r
    | TLeaf _ :: r => Some r
    | TArr n :: r => skips (skip f) n r
    | TMap n :: r => skips (skip f) (n + n) r
    | TTag t :: r =>
      if Nat.eqb t shareable then skip f (strip r)
      else if Nat.eqb t sharedref then
        match r with
        | TUInt _ :: r' => Some r'
        | _ => None
        end
      else skip f r
    end
  end.

Definition next_item (ts : list tok) : option (list tok) := skip (S (length ts)) ts.
Arguments next_item : simpl never.

(* The streams of n items that follow one another. *)
Fixpoint starts (n : nat) (ts : list tok) : list (list tok) :=
  match n with
  | 0 => []
  | S n' =>
    ts :: match next_item ts with
          | Some r => starts n' r
          | None => []
          end
  end.

Section Duplicate.
(* env: the marked items of the stream, entry n for the n-th tag 28. *)
Variable env : list (list tok).

(* resolve takes away a chain of tags 28, and replaces a tag 29 with the marked item that its index names.
   h limits the number of tags 29 that follow one another. *)
Fixpoint resolve (h : nat) (ts : list tok) {struct h} : list tok :=
  match strip ts with
  | TTag t :: TUInt i :: r =>
    if Nat.eqb t sharedref then
      match h with
      | 0 => []
      | S h' => resolve h' (nth i env [])
      end
    else TTag t :: TUInt i :: r
  | c => c
  end.

(* dup reads one item to depth k and duplicates each shared value. An array, a map and a tag other than 28 and
   29 take one depth step. A tag 28 and a tag 29 take none. *)
Fixpoint dup (k : nat) (h : nat) (ts : list tok) {struct k} : tree :=
  match k with
  | 0 => Cut
  | S k' =>
    match resolve h ts with
    | [] => Cut
    | TUInt n :: _ => TU n
    | TLeaf n :: _ => TL n
    | TArr n :: r => TA (map (dup k' (S (length env))) (starts n r))
    | TMap n :: r => TM (tree_pairs (map (dup k' (S (length env))) (starts (n + n) r)))
    | TTag t :: r => TT t (dup k' (S (length env)) r)
    end
  end.

End Duplicate.

(* The duplicating reading of a stream to depth k. *)
Definition expand (k : nat) (ts : list tok) : tree := dup (marks ts) k (S (length (marks ts))) ts.

(* An item that makes a node of its own: no tag 28 and no tag 29 at its head. *)
Definition simple (ts : list tok) : Prop :=
  match ts with
  | TTag t :: _ => t <> shareable /\ t <> sharedref
  | _ => True
  end.

(* Facts about chains of tags 28. *)
Lemma strip_tag : forall t r, strip (TTag t :: r) = if Nat.eqb t shareable then strip r else TTag t :: r.
Proof. reflexivity. Qed.

(* strip and the chain of the reader give the same stream. *)
Lemma strip_chain : forall ts c c' content, chain ts c = (c', content) -> strip ts = content.
Proof.
  induction ts as [| t ts IH]; intros c c' content H; simpl in H.
  - inversion H; subst; reflexivity.
  - destruct t as [n | n | n | n | tg]; try (inversion H; subst; reflexivity).
    rewrite strip_tag. destruct (Nat.eqb tg shareable).
    + eapply IH. exact H.
    + inversion H; subst; reflexivity.
Qed.

Lemma strip_shareable : forall r, strip (TTag shareable :: r) = strip r.
Proof. intros r. reflexivity. Qed.

Lemma strip_simple : forall ts, (forall r, ts <> TTag shareable :: r) -> strip ts = ts.
Proof.
  intros [| [n | n | n | n | t] r] H; try reflexivity.
  rewrite strip_tag. destruct (Nat.eqb t shareable) eqn:M; [| reflexivity].
  apply Nat.eqb_eq in M. subst t. exfalso. eapply H. reflexivity.
Qed.

Lemma strip_not_shareable : forall ts r, strip ts <> TTag shareable :: r.
Proof.
  intros ts. destruct (chain ts 0) as [c content] eqn:Ch.
  rewrite (strip_chain _ _ _ _ Ch). eapply chain_content_head. exact Ch.
Qed.

Lemma strip_strip : forall ts, strip (strip ts) = strip ts.
Proof. intros ts. apply strip_simple. apply strip_not_shareable. Qed.

Lemma strip_length : forall ts, length (strip ts) <= length ts.
Proof.
  intros ts. destruct (chain ts 0) as [c content] eqn:Ch.
  rewrite (strip_chain _ _ _ _ Ch). apply chain_shorter in Ch. lia.
Qed.

Lemma marks_shareable : forall r, marks (TTag shareable :: r) = strip r :: marks r.
Proof. intros r. reflexivity. Qed.

(* A chain of c tags 28 gives c entries, each the item after the chain. *)
Lemma marks_chain : forall r c0 c content, chain r c0 = (c, content) ->
  marks (TTag shareable :: r) = repeat content (S (c - c0)) ++ marks content /\ c0 <= c.
Proof.
  induction r as [| t r IH]; intros c0 c content H; simpl in H.
  - inversion H; subst. rewrite Nat.sub_diag. split; [reflexivity | lia].
  - destruct t as [n | n | n | n | tg];
      try (inversion H; subst; rewrite Nat.sub_diag; split; [reflexivity | lia]).
    destruct (Nat.eqb tg shareable) eqn:M.
    + apply Nat.eqb_eq in M. subst tg.
      destruct (IH _ _ _ H) as [Mr L].
      rewrite marks_shareable, strip_shareable, (strip_chain _ _ _ _ H), Mr.
      split; [| lia].
      replace (c - c0) with (S (c - S c0)) by lia. reflexivity.
    + inversion H; subst. rewrite Nat.sub_diag. split; [| lia].
      rewrite marks_shareable. rewrite strip_simple; [reflexivity |].
      intros r0 E0. injection E0 as E1 _. subst tg. rewrite Nat.eqb_refl in M. discriminate.
Qed.

(* Facts about the duplicating reader. *)
Lemma resolve_strip : forall env h ts, resolve env h ts = resolve env h (strip ts).
Proof. intros env h ts. destruct h; simpl; rewrite strip_strip; reflexivity. Qed.

Lemma resolve_simple : forall env h ts, simple ts -> resolve env h ts = ts.
Proof.
  intros env h ts Hs.
  assert (St : strip ts = ts).
  { apply strip_simple. intros r E0. subst ts. simpl in Hs. apply (proj1 Hs). reflexivity. }
  destruct h; simpl; rewrite St;
    destruct ts as [| [n | n | n | n | t] [| [m | m | m | m | u] r]]; try reflexivity;
    simpl in Hs; destruct Hs as [_ Hr]; apply Nat.eqb_neq in Hr; rewrite Hr; reflexivity.
Qed.

Lemma resolve_ref : forall env h i r, resolve env (S h) (TTag sharedref :: TUInt i :: r) = resolve env h (nth i env []).
Proof.
  intros env h i r. reflexivity.
Qed.

Lemma dup_resolve : forall env k h h' ts ts', resolve env h ts = resolve env h' ts' -> dup env k h ts = dup env k h' ts'.
Proof. intros env [| k] h h' ts ts' H; simpl; [reflexivity | rewrite H; reflexivity]. Qed.

Lemma dup_shareable : forall env k h r c content, chain r 1 = (c, content) ->
  dup env k h (TTag shareable :: r) = dup env k h content.
Proof.
  intros env k h r c content Ch. apply dup_resolve.
  rewrite (resolve_strip env h (TTag shareable :: r)), strip_shareable, (strip_chain _ _ _ _ Ch).
  reflexivity.
Qed.

Lemma dup_ref : forall env k h i r, dup env k (S h) (TTag sharedref :: TUInt i :: r) = dup env k h (nth i env []).
Proof. intros. apply dup_resolve. apply resolve_ref. Qed.

Lemma map_pairs : forall (f : nat -> tree) l,
  map (fun p => (f (fst p), f (snd p))) (pairs l) = tree_pairs (map f l).
Proof.
  intros f. fix IH 1. intros [| a [| b l]]; simpl; [reflexivity | reflexivity |]. rewrite IH. reflexivity.
Qed.

Lemma nth_repeat_lt : forall (A : Type) (a d : A) m n, n < m -> nth n (repeat a m) d = a.
Proof.
  intros A a d. induction m as [| m IH]; intros n L; [lia |].
  destruct n; simpl; [reflexivity | apply IH; lia].
Qed.

(* skip finds the end of the item that read_item reads. *)
Lemma skips_shorter : forall sk, (forall x r, sk x = Some r -> length r < length x) ->
  forall n x r, skips sk n x = Some r -> length r <= length x.
Proof.
  intros sk Hsk. induction n as [| n IH]; intros x r H; simpl in H.
  - inversion H; subst; lia.
  - destruct (sk x) as [r1 |] eqn:E1; [| discriminate].
    apply IH in H. apply Hsk in E1. lia.
Qed.

Lemma skip_shorter : forall f ts r, skip f ts = Some r -> length r < length ts.
Proof.
  induction f as [| f IH]; intros ts r H; [discriminate |].
  simpl in H. destruct ts as [| [n | n | n | n | tg] ts]; try discriminate.
  - inversion H; subst; simpl; lia.
  - inversion H; subst; simpl; lia.
  - apply (skips_shorter _ IH) in H. simpl; lia.
  - apply (skips_shorter _ IH) in H. simpl; lia.
  - destruct (Nat.eqb tg shareable).
    + apply IH in H. pose proof (strip_length ts). simpl; lia.
    + destruct (Nat.eqb tg sharedref).
      * destruct ts as [| [i | | | |] ts']; try discriminate. inversion H; subst; simpl; lia.
      * apply IH in H. simpl; lia.
Qed.

Lemma skips_mono : forall sk sk' L,
  (forall x r, length x <= L -> sk x = Some r -> sk' x = Some r) ->
  (forall x r, sk x = Some r -> length r < length x) ->
  forall n x r, length x <= L -> skips sk n x = Some r -> skips sk' n x = Some r.
Proof.
  intros sk sk' L Hm Hs. induction n as [| n IH]; intros x r L0 H; simpl in *; [exact H |].
  destruct (sk x) as [r1 |] eqn:E1; [| discriminate].
  rewrite (Hm _ _ L0 E1). apply IH; [apply Hs in E1; lia | exact H].
Qed.

Lemma skip_enough : forall f ts r, skip f ts = Some r -> forall f', length ts < f' -> skip f' ts = Some r.
Proof.
  induction f as [| f IH]; intros ts r H f' L; [discriminate |].
  destruct f' as [| f']; [lia |].
  simpl in H |- *. destruct ts as [| [n | n | n | n | tg] ts]; try discriminate; simpl in L.
  - exact H.
  - exact H.
  - eapply (skips_mono (skip f) (skip f') (length ts)); [| apply skip_shorter | lia | exact H].
    intros x r1 Lx E1. apply (IH _ _ E1). lia.
  - eapply (skips_mono (skip f) (skip f') (length ts)); [| apply skip_shorter | lia | exact H].
    intros x r1 Lx E1. apply (IH _ _ E1). lia.
  - destruct (Nat.eqb tg shareable).
    + apply (IH _ _ H). pose proof (strip_length ts). lia.
    + destruct (Nat.eqb tg sharedref); [exact H | apply (IH _ _ H); lia].
Qed.

Lemma items_skips : forall fuel,
  (forall ts s id rest s', read_item fuel ts s = ok (id, rest, s') -> skip fuel ts = Some rest) ->
  forall n ts s ids rest s', items (read_item fuel) n ts s = ok (ids, rest, s') -> skips (skip fuel) n ts = Some rest.
Proof.
  intros fuel Hr. induction n as [| n IH]; intros ts s ids rest s' H; simpl in *.
  - inversion H; subst; reflexivity.
  - destruct (read_item fuel ts s) as [[[id1 rest1] s1] | e] eqn:E1; [| discriminate].
    destruct (items (read_item fuel) n rest1 s1) as [[[ids2 rest2] s2] | e] eqn:E2; [| discriminate].
    inversion H; subst. rewrite (Hr _ _ _ _ _ E1). eapply IH. exact E2.
Qed.

Lemma read_item_skip : forall fuel ts s id rest s', read_item fuel ts s = ok (id, rest, s') -> skip fuel ts = Some rest.
Proof.
  induction fuel as [| fuel IH]; intros ts s id rest s' H; [discriminate |].
  simpl in H |- *. destruct ts as [| [n | n | k | k | tg] ts]; try discriminate.
  - inversion H; subst; reflexivity.
  - inversion H; subst; reflexivity.
  - destruct (items (read_item fuel) k ts (reserved (looked 1 s))) as [[[ids r1] s1] | e] eqn:E; [| discriminate].
    inversion H; subst. eapply items_skips; [exact IH | exact E].
  - destruct (items (read_item fuel) (k + k) ts (reserved (looked 1 s))) as [[[ids r1] s1] | e] eqn:E; [| discriminate].
    inversion H; subst. eapply items_skips; [exact IH | exact E].
  - destruct (Nat.eqb tg shareable).
    + destruct (chain ts 1) as [c content] eqn:Ch.
      destruct (chain_target content c s) as [target | e]; [| discriminate].
      rewrite (strip_chain _ _ _ _ Ch). eapply IH. exact H.
    + destruct (Nat.eqb tg sharedref).
      * destruct ts as [| [i | | | |] ts']; try discriminate.
        destruct (nth_error (table s) i); [| discriminate]. destruct (_ <? _); [| discriminate]. drop_cyclic.
        inversion H; subst; reflexivity.
      * destruct (read_item fuel ts (reserved (looked 1 s))) as [[[c r1] s1] | e] eqn:E; [| discriminate].
        inversion H; subst. eapply IH. exact E.
Qed.

Lemma next_item_read : forall fuel ts s id rest s', read_item fuel ts s = ok (id, rest, s') -> next_item ts = Some rest.
Proof.
  intros fuel ts s id rest s' H. unfold next_item.
  eapply skip_enough; [eapply read_item_skip; exact H | lia].
Qed.

(* The table grows by one entry for each tag 28 that a read passes. *)
Definition marks_step (a : state) (x : list tok) (b : state) (y : list tok) : Prop :=
  exists m, marks x = m ++ marks y /\ length (table b) = length (table a) + length m.

Lemma marks_step_refl : forall s ts, marks_step s ts s ts.
Proof. intros s ts. exists []. split; [reflexivity | simpl; lia]. Qed.

Lemma marks_step_trans : forall a b c x y z, marks_step a x b y -> marks_step b y c z -> marks_step a x c z.
Proof.
  intros a b c x y z [m1 [M1 L1]] [m2 [M2 L2]]. exists (m1 ++ m2).
  split; [rewrite M1, M2, app_assoc; reflexivity | rewrite length_app; lia].
Qed.

Lemma read_item_marks : forall fuel ts s id rest s', read_item fuel ts s = ok (id, rest, s') -> marks_step s ts s' rest.
Proof.
  induction fuel as [| fuel IH]; intros ts s id rest s' H; [discriminate |].
  simpl in H. destruct ts as [| t ts]; [discriminate |].
  assert (Hit : forall k ts0 s0 ids r0 s0', items (read_item fuel) k ts0 s0 = ok (ids, r0, s0') -> marks_step s0 ts0 s0' r0).
  { intros. eapply (items_step _ marks_step marks_step_refl marks_step_trans); [| eassumption].
    intros; eapply IH; eassumption. }
  destruct t as [n | n | k | k | tg].
  - inversion H; subst. exists []. split; [reflexivity | simpl; lia].
  - inversion H; subst. exists []. split; [reflexivity | simpl; lia].
  - destruct (items (read_item fuel) k ts (reserved (looked 1 s))) as [[[ids r1] s1] | e] eqn:E; [| discriminate].
    inversion H; subst. destruct (Hit _ _ _ _ _ _ E) as [m [Mm L]]. exists m. simpl in *. split; [exact Mm | lia].
  - destruct (items (read_item fuel) (k + k) ts (reserved (looked 1 s))) as [[[ids r1] s1] | e] eqn:E; [| discriminate].
    inversion H; subst. destruct (Hit _ _ _ _ _ _ E) as [m [Mm L]]. exists m. simpl in *. split; [exact Mm | lia].
  - destruct (Nat.eqb tg shareable) eqn:M.
    + apply Nat.eqb_eq in M. subst tg.
      destruct (chain ts 1) as [c content] eqn:Ch.
      destruct (chain_target content c s) as [target | e]; [| discriminate].
      destruct (IH _ _ _ _ _ H) as [m [Mm L]].
      destruct (marks_chain _ _ _ _ Ch) as [Mc C1].
      exists (repeat content (S (c - 1)) ++ m). split.
      * rewrite Mc, Mm, app_assoc. reflexivity.
      * simpl in L. rewrite length_app, repeat_length in L. rewrite length_app, repeat_length. lia.
    + destruct (Nat.eqb tg sharedref) eqn:R.
      * destruct ts as [| [i | | | |] ts']; try discriminate.
        destruct (nth_error (table s) i); [| discriminate]. destruct (_ <? _); [| discriminate]. drop_cyclic.
        inversion H; subst. exists []. split; [simpl; rewrite M; reflexivity | simpl; lia].
      * destruct (read_item fuel ts (reserved (looked 1 s))) as [[[c r1] s1] | e] eqn:E; [| discriminate].
        inversion H; subst. destruct (IH _ _ _ _ _ E) as [m [Mm L]]. exists m.
        split; [simpl; rewrite M; exact Mm | simpl in *; lia].
Qed.

Lemma items_extends : forall fuel n ts s ids rest s',
  items (read_item fuel) n ts s = ok (ids, rest, s') -> extends s s'.
Proof.
  intros fuel n ts s ids rest s' H.
  eapply (items_step _ (fun a _ b _ => extends a b)); [| | | exact H].
  - intros; apply extends_refl.
  - intros; eapply extends_trans; eassumption.
  - intros ts0 s0 id0 r0 s0' E0. exact (proj1 (read_item_extends _ _ _ _ _ _ E0)).
Qed.

(* Each id enters the graph once. *)
Definition fresh_step (a : state) (x : list tok) (b : state) (y : list tok) : Prop :=
  next a <= next b /\ forall e, In e (graph b) -> In e (graph a) \/ next a <= fst e.

Lemma fresh_refl : forall s ts, fresh_step s ts s ts.
Proof. intros; split; [lia | intros e I; left; exact I]. Qed.

Lemma fresh_trans : forall a b c x y z, fresh_step a x b y -> fresh_step b y c z -> fresh_step a x c z.
Proof.
  intros a b c x y z [N1 F1] [N2 F2]. split; [lia |]. intros e I.
  destruct (F2 e I) as [I1 | L]; [destruct (F1 e I1) as [I2 | L2]; [left; exact I2 | right; lia] | right; lia].
Qed.

Lemma read_item_fresh : forall fuel ts s id rest s', read_item fuel ts s = ok (id, rest, s') -> fresh_step s ts s' rest.
Proof.
  induction fuel as [| fuel IH]; intros ts s id rest s' H; [discriminate |].
  destruct (read_item_extends _ _ _ _ _ _ H) as [[_ [_ N]] _].
  split; [exact N |].
  simpl in H. destruct ts as [| t ts]; [discriminate |].
  assert (Hit : forall k ts0 s0 ids r0 s0', items (read_item fuel) k ts0 s0 = ok (ids, r0, s0') -> fresh_step s0 ts0 s0' r0).
  { intros. eapply (items_step _ fresh_step fresh_refl fresh_trans); [| eassumption].
    intros; eapply IH; eassumption. }
  destruct t as [n | n | k | k | tg].
  - inversion H; subst. intros e [<- | I]; [right; simpl; lia | left; exact I].
  - inversion H; subst. intros e [<- | I]; [right; simpl; lia | left; exact I].
  - destruct (items (read_item fuel) k ts (reserved (looked 1 s))) as [[[ids r1] s1] | e] eqn:E; [| discriminate].
    inversion H; subst. destruct (Hit _ _ _ _ _ _ E) as [_ F1].
    intros e [<- | I]; [right; simpl; lia |].
    destruct (F1 e I) as [I1 | L]; [left; exact I1 | right; simpl in L; lia].
  - destruct (items (read_item fuel) (k + k) ts (reserved (looked 1 s))) as [[[ids r1] s1] | e] eqn:E; [| discriminate].
    inversion H; subst. destruct (Hit _ _ _ _ _ _ E) as [_ F1].
    intros e [<- | I]; [right; simpl; lia |].
    destruct (F1 e I) as [I1 | L]; [left; exact I1 | right; simpl in L; lia].
  - destruct (Nat.eqb tg shareable).
    + destruct (chain ts 1) as [c content].
      destruct (chain_target content c s) as [target | e]; [| discriminate].
      destruct (IH _ _ _ _ _ H) as [_ F1]. exact F1.
    + destruct (Nat.eqb tg sharedref).
      * destruct ts as [| [i | | | |] ts']; try discriminate.
        destruct (nth_error (table s) i); [| discriminate]. destruct (_ <? _); [| discriminate]. drop_cyclic.
        inversion H; subst. intros e I; left; exact I.
      * destruct (read_item fuel ts (reserved (looked 1 s))) as [[[c r1] s1] | e] eqn:E; [| discriminate].
        inversion H; subst. destruct (IH _ _ _ _ _ E) as [_ F1].
        intros e [<- | I]; [right; simpl; lia |].
        destruct (F1 e I) as [I1 | L]; [left; exact I1 | right; simpl in L; lia].
Qed.

Lemma sound_rl : forall s, sound s -> sound (reserved (looked 1 s)).
Proof.
  intros s S i n Hi. destruct (S i n Hi) as [L F]. simpl.
  split; [lia | refine (Forall_impl _ _ F); intros; lia].
Qed.

Lemma unique_made : forall s s1 n, sound s -> NoDup (map fst (graph s1)) ->
  (forall e, In e (graph s1) -> In e (graph s) \/ S (next s) <= fst e) ->
  NoDup (map fst (graph (made (next s) n s1))).
Proof.
  intros s s1 n S N F. simpl. constructor; [| exact N].
  intros I. apply in_map_iff in I as [[i m] [Ei Ii]]. simpl in Ei. subst i.
  destruct (F _ Ii) as [I1 | L]; [destruct (S _ _ I1) as [L _]; lia | simpl in L; lia].
Qed.

Definition unique_step (a : state) (x : list tok) (b : state) (y : list tok) : Prop :=
  sound a -> NoDup (map fst (graph a)) -> sound b /\ NoDup (map fst (graph b)).

Lemma unique_refl : forall s ts, unique_step s ts s ts.
Proof. intros s ts S N. split; assumption. Qed.

Lemma unique_trans : forall a b c x y z, unique_step a x b y -> unique_step b y c z -> unique_step a x c z.
Proof. intros a b c x y z H1 H2 S N. destruct (H1 S N) as [S1 N1]. apply H2; assumption. Qed.

Lemma read_item_unique : forall fuel ts s id rest s', read_item fuel ts s = ok (id, rest, s') -> unique_step s ts s' rest.
Proof.
  induction fuel as [| fuel IH]; intros ts s id rest s' H S N; [discriminate |].
  split; [exact (proj1 (read_item_sound _ _ _ _ _ _ H S)) |].
  simpl in H. destruct ts as [| t ts]; [discriminate |].
  assert (Hit : forall k ts0 s0 ids r0 s0', items (read_item fuel) k ts0 s0 = ok (ids, r0, s0') -> unique_step s0 ts0 s0' r0).
  { intros. eapply (items_step _ unique_step unique_refl unique_trans); [| eassumption].
    intros; eapply IH; eassumption. }
  assert (Fit : forall k ts0 s0 ids r0 s0', items (read_item fuel) k ts0 s0 = ok (ids, r0, s0') -> fresh_step s0 ts0 s0' r0).
  { intros. eapply (items_step _ fresh_step fresh_refl fresh_trans); [| eassumption].
    intros; eapply read_item_fresh; eassumption. }
  destruct t as [n | n | k | k | tg].
  - inversion H; subst. apply unique_made; [exact S | exact N | intros e I; left; exact I].
  - inversion H; subst. apply unique_made; [exact S | exact N | intros e I; left; exact I].
  - destruct (items (read_item fuel) k ts (reserved (looked 1 s))) as [[[ids r1] s1] | e] eqn:E; [| discriminate].
    inversion H; subst.
    apply unique_made; [exact S | exact (proj2 (Hit _ _ _ _ _ _ E (sound_rl _ S) N)) | exact (proj2 (Fit _ _ _ _ _ _ E))].
  - destruct (items (read_item fuel) (k + k) ts (reserved (looked 1 s))) as [[[ids r1] s1] | e] eqn:E; [| discriminate].
    inversion H; subst.
    apply unique_made; [exact S | exact (proj2 (Hit _ _ _ _ _ _ E (sound_rl _ S) N)) | exact (proj2 (Fit _ _ _ _ _ _ E))].
  - destruct (Nat.eqb tg shareable).
    + destruct (chain ts 1) as [c content].
      destruct (chain_target content c s) as [target | e]; [| discriminate].
      exact (proj2 (IH _ _ _ _ _ H S N)).
    + destruct (Nat.eqb tg sharedref).
      * destruct ts as [| [i | | | |] ts']; try discriminate.
        destruct (nth_error (table s) i); [| discriminate]. destruct (_ <? _); [| discriminate]. drop_cyclic.
        inversion H; subst. exact N.
      * destruct (read_item fuel ts (reserved (looked 1 s))) as [[[c r1] s1] | e] eqn:E; [| discriminate].
        inversion H; subst.
        apply unique_made; [exact S | exact (proj2 (IH _ _ _ _ _ E (sound_rl _ S) N)) |
                            exact (proj2 (read_item_fresh _ _ _ _ _ _ E))].
Qed.

(* No two nodes of the graph of a successful read have the same id. *)
Theorem read_unique : forall ts root s, read ts = ok (root, s) -> NoDup (map fst (graph s)).
Proof.
  intros ts root s H. unfold read in H.
  destruct (read_item (S (length ts)) ts start) as [[[r rest] s'] | e] eqn:E; [| discriminate].
  destruct rest; [| discriminate]. inversion H; subst.
  edestruct (read_item_unique _ _ _ _ _ _ E) as [_ N]; [intros i n [] | constructor | exact N].
Qed.

Lemma find_first : forall g0 i n r, NoDup (map fst (g0 ++ (i, n) :: r)) -> find (g0 ++ (i, n) :: r) i = Some n.
Proof.
  induction g0 as [| [j m] g0 IH]; intros i n r N; simpl.
  - rewrite Nat.eqb_refl. reflexivity.
  - simpl in N. inversion N as [| ? ? Nj N']; subst.
    destruct (Nat.eqb j i) eqn:J.
    + apply Nat.eqb_eq in J. subst j. exfalso. apply Nj.
      rewrite map_app. apply in_or_app. right. left. reflexivity.
    + apply IH. exact N'.
Qed.

Lemma find_in : forall g i n, In (i, n) g -> exists m, find g i = Some m.
Proof.
  induction g as [| [j m] g IH]; intros i n I; [destruct I |]. simpl.
  destruct (Nat.eqb j i) eqn:J; [eexists; reflexivity |].
  destruct I as [Ei | I].
  - injection Ei as Ej _. subst j. rewrite Nat.eqb_refl in J. discriminate.
  - eapply IH. exact I.
Qed.

Lemma find_some_in : forall g i n, find g i = Some n -> In (i, n) g.
Proof.
  induction g as [| [j m] g IH]; intros i n H; simpl in H; [discriminate |].
  destruct (Nat.eqb j i) eqn:J.
  - apply Nat.eqb_eq in J. inversion H; subst. left. reflexivity.
  - right. apply IH. exact H.
Qed.

(* The choice of Cut for an id that is not in the graph never matters: after a successful read, the root and each
   child of each node are in the graph. *)
Theorem read_no_missing : forall ts root s, read ts = ok (root, s) ->
  find (graph s) root <> None /\
  forall i n, find (graph s) i = Some n -> Forall (fun c => find (graph s) c <> None) (children n).
Proof.
  intros ts root s H. destruct (read_no_dangling _ _ _ H) as [Lr [S C]].
  assert (A : forall i, i < next s -> find (graph s) i <> None).
  { intros i Li. destruct (C i Li) as [n In0]. destruct (find_in _ _ _ In0) as [m Fm]. rewrite Fm. discriminate. }
  split; [apply A; exact Lr |].
  intros i n Fi. apply find_some_in in Fi. destruct (S i n Fi) as [_ Fc].
  refine (Forall_impl _ _ Fc). intros c Lc. apply A. exact Lc.
Qed.

Lemma chain_target_cases : forall content c s target, chain_target content c s = ok target ->
  (exists i r0, content = TTag sharedref :: TUInt i :: r0 /\ nth_error (table s) i = Some target) \/
  (target = next s /\ forall t r0, content = TTag t :: r0 -> t <> sharedref).
Proof.
  intros content c s target H. unfold chain_target in H.
  destruct content as [| tk r].
  { inversion H; subst. right. split; [reflexivity | intros t0 r0 E0; discriminate E0]. }
  destruct tk as [n | n | n | n | t];
    try (inversion H; subst; right; split; [reflexivity | intros t0 r0 E0; discriminate E0]).
  destruct (Nat.eqb t sharedref) eqn:R.
  - apply Nat.eqb_eq in R. subst t. left.
    destruct r as [| [i | | | |] r']; try discriminate.
    destruct (nth_error (table s) i) as [x |] eqn:Ex.
    + destruct (x <? next s); [| discriminate]. inversion H; subst. exists i, r'. split; [reflexivity | exact Ex].
    + destruct (_ <? _); discriminate.
  - inversion H; subst. right. split; [reflexivity |].
    intros t0 r0 E0. injection E0 as E1 _. subst t0. apply Nat.eqb_neq. exact R.
Qed.

Lemma simple_of : forall ts, (forall r, ts <> TTag shareable :: r) -> (forall t r, ts = TTag t :: r -> t <> sharedref) -> simple ts.
Proof.
  intros [| [n | n | n | n | t] r] A B; simpl; auto.
  split; [intros E0; subst t; eapply A; reflexivity | eapply B; reflexivity].
Qed.

Section Meaning.
(* One successful read: the stream T, its root and its final state F. *)
Variable T : list tok.
Variable root : nat.
Variable F : state.
Hypothesis Hread : read T = ok (root, F).

Local Abbreviation gF := (graph F).
Local Abbreviation mT := (marks T).
Local Abbreviation hT := (S (length (marks T))).

(* The graph of state s is a part of the final graph at its end. *)
Definition Gsuffix (s : state) : Prop := exists g0, gF = g0 ++ graph s.

(* The stream ts follows the marks that the table tb counts. *)
Definition Msuffix (ts : list tok) (tb : list nat) : Prop := exists pre, mT = pre ++ marks ts /\ length pre = length tb.

(* Each entry of the table unfolds as its marked item reads, to each depth up to k. *)
Definition Good (k : nat) (tb : list nat) : Prop :=
  forall j i h, j <= k -> i < length tb -> i < h -> unfold gF j (nth i tb 0) = dup mT j h (nth i mT []).

(* A read of one item keeps the table good and gives a node that unfolds as the item reads, at depth k. *)
Definition P (k : nat) : Prop :=
  forall fuel ts s id rest s', read_item fuel ts s = ok (id, rest, s') ->
    Gsuffix s' -> Msuffix ts (table s) -> Good k (table s) ->
    Good k (table s') /\ unfold gF k id = dup mT k hT ts.

Lemma unique_F : NoDup (map fst gF).
Proof. exact (read_unique T root F Hread). Qed.

Lemma Gsuffix_back : forall a b, extends a b -> Gsuffix b -> Gsuffix a.
Proof. intros a b [[g Gb] _] [g0 G0]. exists (g0 ++ g). rewrite G0, Gb, app_assoc. reflexivity. Qed.

Lemma find_made : forall id n s1, Gsuffix (made id n s1) -> find gF id = Some n.
Proof.
  intros id n s1 [g0 G0]. change (graph (made id n s1)) with ((id, n) :: graph s1) in G0.
  pose proof unique_F as N. rewrite G0 in N |- *. apply find_first. exact N.
Qed.

Lemma Msuffix_read : forall fuel ts a id rest b, read_item fuel ts a = ok (id, rest, b) ->
  Msuffix ts (table a) -> Msuffix rest (table b).
Proof.
  intros fuel ts a id rest b H [pre [Me L]]. destruct (read_item_marks _ _ _ _ _ _ H) as [m [Mm Lm]].
  exists (pre ++ m). split; [rewrite Me, Mm, app_assoc; reflexivity | rewrite length_app; lia].
Qed.

Lemma Msuffix_le : forall ts tb, Msuffix ts tb -> length tb <= length mT.
Proof. intros ts tb [pre [Me L]]. rewrite Me, length_app. lia. Qed.

Lemma Msuffix_tag : forall t r tb, Nat.eqb t shareable = false -> Msuffix (TTag t :: r) tb -> Msuffix r tb.
Proof.
  intros t r tb M [pre [Me L]]. exists pre. split; [rewrite Me; simpl; rewrite M; reflexivity | exact L].
Qed.

Lemma Good_le : forall k j tb, Good k tb -> j <= k -> Good j tb.
Proof. intros k j tb Gk Jk j' i h J'. apply Gk. lia. Qed.

Lemma Good_app : forall k tb target c content, Good k tb ->
  (forall j h, j <= k -> length tb < h -> unfold gF j target = dup mT j h content) ->
  (forall m, m < c -> nth (length tb + m) mT [] = content) ->
  Good k (tb ++ repeat target c).
Proof.
  intros k tb target c content Gk Ht Hn j i h Jk Li Lh.
  rewrite length_app, repeat_length in Li.
  destruct (Nat.lt_ge_cases i (length tb)) as [L | L].
  - rewrite app_nth1 by exact L. apply Gk; assumption.
  - rewrite app_nth2 by exact L. rewrite nth_repeat_lt by lia.
    replace i with (length tb + (i - length tb)) by lia. rewrite Hn by lia. apply Ht; lia.
Qed.

Lemma items_meaning : forall fuel j,
  (forall ts a id rest b, read_item fuel ts a = ok (id, rest, b) -> Gsuffix b -> Msuffix ts (table a) ->
     Good j (table a) -> Good j (table b) /\ unfold gF j id = dup mT j hT ts) ->
  forall n ts a ids rest b, items (read_item fuel) n ts a = ok (ids, rest, b) -> Gsuffix b ->
    Msuffix ts (table a) -> Good j (table a) ->
    Good j (table b) /\ map (unfold gF j) ids = map (dup mT j hT) (starts n ts).
Proof.
  intros fuel j Hr. induction n as [| n IH]; intros ts a ids rest b H Gs Ms Gd; simpl in H.
  - inversion H; subst. split; [exact Gd | reflexivity].
  - destruct (read_item fuel ts a) as [[[id1 rest1] s1] | e] eqn:E1; [| discriminate].
    destruct (items (read_item fuel) n rest1 s1) as [[[ids2 rest2] s2] | e] eqn:E2; [| discriminate].
    inversion H; subst.
    assert (X : extends s1 b) by (eapply items_extends; exact E2).
    destruct (Hr _ _ _ _ _ E1 (Gsuffix_back _ _ X Gs) Ms Gd) as [Gd1 U1].
    destruct (IH _ _ _ _ _ E2 Gs (Msuffix_read _ _ _ _ _ _ E1 Ms) Gd1) as [Gd2 U2].
    split; [exact Gd2 |].
    simpl. rewrite (next_item_read _ _ _ _ _ _ E1). simpl. rewrite U1, U2. reflexivity.
Qed.

(* A read of an item that makes a node: its node unfolds to depth S k as the item reads, when the reads of depth
   k hold. *)
Lemma shallow : forall k, P k -> forall fuel ts s id rest s', simple ts -> read_item fuel ts s = ok (id, rest, s') ->
  Gsuffix s' -> Msuffix ts (table s) -> Good k (table s) -> forall h, unfold gF (S k) id = dup mT (S k) h ts.
Proof.
  intros k Pk fuel ts s id rest s' Hs H Gs Ms Gd h.
  destruct fuel as [| fuel]; [discriminate |].
  simpl. rewrite (resolve_simple _ h ts Hs).
  simpl in H. destruct ts as [| [n | n | n | n | t] r]; [discriminate | | | | |].
  - inversion H; subst. rewrite (find_made _ _ _ Gs). reflexivity.
  - inversion H; subst. rewrite (find_made _ _ _ Gs). reflexivity.
  - destruct (items (read_item fuel) n r (reserved (looked 1 s))) as [[[ids r1] s1] | e] eqn:Ei; [| discriminate].
    inversion H; subst. rewrite (find_made _ _ _ Gs). f_equal.
    exact (proj2 (items_meaning fuel k (Pk fuel) n r _ _ _ _ Ei
                    (Gsuffix_back _ _ (extends_made _ _ _) Gs) Ms Gd)).
  - destruct (items (read_item fuel) (n + n) r (reserved (looked 1 s))) as [[[ids r1] s1] | e] eqn:Ei; [| discriminate].
    inversion H; subst. rewrite (find_made _ _ _ Gs). rewrite (map_pairs (unfold gF k)). f_equal. f_equal.
    exact (proj2 (items_meaning fuel k (Pk fuel) (n + n) r _ _ _ _ Ei
                    (Gsuffix_back _ _ (extends_made _ _ _) Gs) Ms Gd)).
  - destruct Hs as [N28 N29]. apply Nat.eqb_neq in N28, N29. rewrite N28, N29 in H.
    destruct (read_item fuel r (reserved (looked 1 s))) as [[[c r1] s1] | e] eqn:Ec; [| discriminate].
    inversion H; subst. rewrite (find_made _ _ _ Gs). f_equal.
    exact (proj2 (Pk fuel r _ _ _ _ Ec (Gsuffix_back _ _ (extends_made _ _ _) Gs) (Msuffix_tag _ _ _ N28 Ms) Gd)).
Qed.

Lemma P_step : forall k, (forall j, j < k -> P j) -> P k.
Proof.
  intros k Plt.
  assert (Sh : forall j, j <= k -> forall fuel ts s id rest s', simple ts -> read_item fuel ts s = ok (id, rest, s') ->
            Gsuffix s' -> Msuffix ts (table s) -> Good (pred j) (table s) -> forall h, unfold gF j id = dup mT j h ts).
  { intros [| j] Jk; intros; [reflexivity |]. eapply shallow; [apply Plt; lia | eassumption ..]. }
  unfold P. induction fuel as [| fuel IH]; intros ts s id rest s' H Gs Ms Gd; [discriminate |].
  assert (Simple : simple ts -> unfold gF k id = dup mT k hT ts).
  { intros Hs. eapply Sh; [lia | exact Hs | exact H | exact Gs | exact Ms | eapply Good_le; [exact Gd | lia]]. }
  destruct ts as [| tk r]; [discriminate |].
  destruct tk as [n | n | n | n | t].
  - simpl in H. inversion H; subst. split; [exact Gd | apply Simple; exact I].
  - simpl in H. inversion H; subst. split; [exact Gd | apply Simple; exact I].
  - simpl in H.
    destruct (items (read_item fuel) n r (reserved (looked 1 s))) as [[[ids r1] s1] | e] eqn:Ei; [| discriminate].
    inversion H; subst. split; [| apply Simple; exact I].
    exact (proj1 (items_meaning fuel k IH n r _ _ _ _ Ei (Gsuffix_back _ _ (extends_made _ _ _) Gs) Ms Gd)).
  - simpl in H.
    destruct (items (read_item fuel) (n + n) r (reserved (looked 1 s))) as [[[ids r1] s1] | e] eqn:Ei; [| discriminate].
    inversion H; subst. split; [| apply Simple; exact I].
    exact (proj1 (items_meaning fuel k IH (n + n) r _ _ _ _ Ei (Gsuffix_back _ _ (extends_made _ _ _) Gs) Ms Gd)).
  - destruct (Nat.eqb t shareable) eqn:M28.
    + (* A chain of tags 28: its entries name the node of the item after the chain. *)
      simpl in H. rewrite M28 in H. apply Nat.eqb_eq in M28. subst t.
      destruct (chain r 1) as [c content] eqn:Ch.
      destruct (chain_target content c s) as [target | e] eqn:CT; [| discriminate].
      destruct (marks_chain _ _ _ _ Ch) as [Mc C1].
      assert (Ms1 : Msuffix content (table s ++ repeat target c)).
      { destruct Ms as [pre [Me L]]. exists (pre ++ repeat content c). split.
        - rewrite Me, Mc, <- app_assoc. replace (S (c - 1)) with c by lia. reflexivity.
        - rewrite !length_app, !repeat_length. lia. }
      assert (Nth : forall m, m < c -> nth (length (table s) + m) mT [] = content).
      { destruct Ms as [pre [Me L]]. intros m Lm. rewrite Me, Mc.
        rewrite app_nth2 by lia. replace (length (table s) + m - length pre) with m by lia.
        rewrite app_nth1 by (rewrite repeat_length; lia). apply nth_repeat_lt. lia. }
      assert (Gd1 : Good k (table s ++ repeat target c)).
      { destruct (chain_target_cases content c s target CT) as [[i0 [r0 [Ec Ei]]] | [Et Hc]].
        - (* The chain marks a reference: its entries name the node of an earlier entry. *)
          apply (Good_app k (table s) target c content Gd); [| exact Nth].
          intros j h Jk Lh. subst content. destruct h as [| h]; [lia |]. rewrite dup_ref.
          assert (Tg : nth i0 (table s) 0 = target) by (apply nth_error_nth; exact Ei).
          assert (Li : i0 < length (table s)) by (apply nth_error_Some; rewrite Ei; discriminate).
          rewrite <- Tg. apply Gd; lia.
        - (* The chain marks an item that makes a node: the node gets its id before its content is read. *)
          assert (Hs : simple content) by (apply simple_of; [eapply chain_content_head; exact Ch | exact Hc]).
          assert (Iid : id = target).
          { eapply (chain_target_is_node fuel content c s (marked_as target c (looked c s))); [exact CT | eapply chain_content_head; exact Ch |
                                                            reflexivity | exists (repeat target c); reflexivity | exact H]. }
          assert (R : forall j, j <= k -> forall j', j' <= j -> forall h, unfold gF j' target = dup mT j' h content).
          { induction j as [| j IHj]; intros Jk j' Jj' h.
            - replace j' with 0 by lia. reflexivity.
            - destruct (Nat.eq_dec j' (S j)) as [-> | D]; [| apply IHj; lia].
              rewrite <- Iid. eapply (Sh (S j)); [exact Jk | exact Hs | exact H | exact Gs | exact Ms1 |].
              simpl pred. apply (Good_app j (table s) target c content); [eapply Good_le; [exact Gd | lia] | | exact Nth].
              intros j0 h0 J0 _. apply IHj; lia. }
          apply (Good_app k (table s) target c content Gd); [| exact Nth].
          intros j h Jk _. exact (R j Jk j (le_n j) h). }
      destruct (IH _ _ _ _ _ H Gs Ms1 Gd1) as [Gd2 U].
      split; [exact Gd2 |]. rewrite U. symmetry. apply (dup_shareable _ _ _ _ c). exact Ch.
    + destruct (Nat.eqb t sharedref) eqn:M29.
      * (* A tag 29: its id is the entry that its index names. *)
        simpl in H. rewrite M28, M29 in H. apply Nat.eqb_eq in M29. subst t.
        destruct r as [| [i | | | |] r']; try discriminate.
        destruct (nth_error (table s) i) as [x |] eqn:Ex; [| discriminate].
        destruct (x <? next s); [| discriminate]. drop_cyclic.
        inversion H; subst. split; [exact Gd |].
        rewrite dup_ref.
        assert (Tg : nth i (table s) 0 = id) by (apply nth_error_nth; exact Ex).
        assert (Li : i < length (table s)) by (apply nth_error_Some; rewrite Ex; discriminate).
        pose proof (Msuffix_le _ _ Ms) as Lm.
        rewrite <- Tg. apply Gd; lia.
      * (* Another tag: its node holds the node of its content. *)
        simpl in H. rewrite M28, M29 in H.
        destruct (read_item fuel r (reserved (looked 1 s))) as [[[c r1] s1] | e] eqn:Ec; [| discriminate].
        inversion H; subst. split.
        -- exact (proj1 (IH _ _ _ _ _ Ec (Gsuffix_back _ _ (extends_made _ _ _) Gs) (Msuffix_tag _ _ _ M28 Ms) Gd)).
        -- apply Simple. simpl. split; apply Nat.eqb_neq; assumption.
Qed.

Lemma P_all : forall k, P k.
Proof.
  assert (A : forall k j, j <= k -> P j).
  { induction k as [| k IHk]; intros j Jk; apply P_step; intros j' L; [lia | apply IHk; lia]. }
  intros k. apply (A k k). lia.
Qed.

Lemma meaning_of_read : forall k, unfold gF k root = dup mT k hT T.
Proof.
  intros k. pose proof Hread as R. unfold read in R.
  destruct (read_item (S (length T)) T start) as [[[r0 rest] s] | e] eqn:Er; [| discriminate].
  destruct rest; [| discriminate]. injection R as R1 R2. subst r0 s.
  refine (proj2 (P_all k _ _ _ _ _ _ Er _ _ _)).
  - exists []. reflexivity.
  - exists []. split; reflexivity.
  - intros j i h _ Li. simpl in Li. lia.
Qed.

End Meaning.

(* T7: the graph of a successful read means what the duplicating reader reads. *)
Theorem read_meaning : forall ts root s, read ts = ok (root, s) -> forall k, unfold (graph s) k root = expand k ts.
Proof. intros ts root s H k. exact (meaning_of_read ts root s H k). Qed.

(* T6: when read succeeds, from the stream alone. *)

(* item_end gives the stream after one item. Each head takes its content: an array n items, a map 2n items, a tag
   one item. *)
Fixpoint item_end (fuel : nat) (ts : list tok) : option (list tok) :=
  match fuel with
  | 0 => None
  | S f =>
    match ts with
    | [] => None
    | TUInt _ :: r => Some r
    | TLeaf _ :: r => Some r
    | TArr n :: r => skips (item_end f) n r
    | TMap n :: r => skips (item_end f) (n + n) r
    | TTag _ :: r => item_end f r
    end
  end.

(* The stream is one item, and nothing follows it. *)
Definition well_formed (ts : list tok) : Prop := item_end (S (length ts)) ts = Some [].

Lemma item_end_tags : forall k f x, item_end (k + f) (repeat (TTag shareable) k ++ x) = item_end f x.
Proof. induction k as [| k IH]; intros f x; [reflexivity | exact (IH f x)]. Qed.

Lemma chain_repeat : forall r c0 c content, chain r c0 = (c, content) ->
  r = repeat (TTag shareable) (c - c0) ++ content /\ c0 <= c.
Proof.
  induction r as [| t r IH]; intros c0 c content H; simpl in H.
  - inversion H; subst. rewrite Nat.sub_diag. split; [reflexivity | lia].
  - destruct t as [n | n | n | n | tg]; try (inversion H; subst; rewrite Nat.sub_diag; split; [reflexivity | lia]).
    destruct (Nat.eqb tg shareable) eqn:M.
    + apply Nat.eqb_eq in M. subst tg. destruct (IH _ _ _ H) as [Rr L].
      split; [| lia]. replace (c - c0) with (S (c - S c0)) by lia. simpl. rewrite Rr. reflexivity.
    + inversion H; subst. rewrite Nat.sub_diag. split; [reflexivity | lia].
Qed.

Lemma chain_whole : forall r c content, chain r 1 = (c, content) ->
  TTag shareable :: r = repeat (TTag shareable) c ++ content /\ 1 <= c.
Proof.
  intros r c content Ch. destruct (chain_repeat _ _ _ _ Ch) as [Rr L]. split; [| exact L].
  rewrite Rr. destruct c as [| c]; [lia |]. simpl. rewrite Nat.sub_0_r. reflexivity.
Qed.

Lemma item_end_shorter : forall f ts r, item_end f ts = Some r -> length r < length ts.
Proof.
  induction f as [| f IH]; intros ts r H; [discriminate |].
  simpl in H. destruct ts as [| [n | n | n | n | tg] ts]; try discriminate.
  - inversion H; subst; simpl; lia.
  - inversion H; subst; simpl; lia.
  - apply (skips_shorter _ IH) in H. simpl; lia.
  - apply (skips_shorter _ IH) in H. simpl; lia.
  - apply IH in H. simpl; lia.
Qed.

Lemma item_end_enough : forall f ts r, item_end f ts = Some r -> forall f', length ts < f' -> item_end f' ts = Some r.
Proof.
  induction f as [| f IH]; intros ts r H f' L; [discriminate |].
  destruct f' as [| f']; [lia |].
  simpl in H |- *. destruct ts as [| [n | n | n | n | tg] ts]; try discriminate; simpl in L.
  - exact H.
  - exact H.
  - eapply (skips_mono (item_end f) (item_end f') (length ts)); [| apply item_end_shorter | lia | exact H].
    intros x r1 Lx E1. apply (IH _ _ E1). lia.
  - eapply (skips_mono (item_end f) (item_end f') (length ts)); [| apply item_end_shorter | lia | exact H].
    intros x r1 Lx E1. apply (IH _ _ E1). lia.
  - apply (IH _ _ H). lia.
Qed.

(* A successful read reads one whole item. *)
Lemma items_ends : forall fuel F,
  (forall ts s id rest s', read_item fuel ts s = ok (id, rest, s') -> forall f, length ts < f -> item_end f ts = Some rest) ->
  forall n x a ids rest b, items (read_item fuel) n x a = ok (ids, rest, b) -> length x < F ->
    skips (item_end F) n x = Some rest.
Proof.
  intros fuel F Hr. induction n as [| n IH]; intros x a ids rest b H L; simpl in H |- *.
  - inversion H; subst; reflexivity.
  - destruct (read_item fuel x a) as [[[id1 rest1] s1] | e] eqn:E1; [| discriminate].
    destruct (items (read_item fuel) n rest1 s1) as [[[ids2 rest2] s2] | e] eqn:E2; [| discriminate].
    inversion H; subst. rewrite (Hr _ _ _ _ _ E1 F L).
    eapply IH; [exact E2 |]. destruct (read_item_extends _ _ _ _ _ _ E1) as [_ L1]. lia.
Qed.

Lemma read_item_ends : forall fuel ts s id rest s', read_item fuel ts s = ok (id, rest, s') ->
  forall f, length ts < f -> item_end f ts = Some rest.
Proof.
  induction fuel as [| fuel IH]; intros ts s id rest s' H f L; [discriminate |].
  destruct f as [| f]; [lia |].
  simpl in H. destruct ts as [| [n | n | k | k | tg] ts]; try discriminate; simpl in L.
  - inversion H; subst; reflexivity.
  - inversion H; subst; reflexivity.
  - destruct (items (read_item fuel) k ts (reserved (looked 1 s))) as [[[ids r1] s1] | e] eqn:E; [| discriminate].
    inversion H; subst. simpl. eapply items_ends; [exact IH | exact E | lia].
  - destruct (items (read_item fuel) (k + k) ts (reserved (looked 1 s))) as [[[ids r1] s1] | e] eqn:E; [| discriminate].
    inversion H; subst. simpl. eapply items_ends; [exact IH | exact E | lia].
  - destruct (Nat.eqb tg shareable) eqn:M.
    + apply Nat.eqb_eq in M. subst tg.
      destruct (chain ts 1) as [c content] eqn:Ch.
      destruct (chain_target content c s) as [target | e]; [| discriminate].
      destruct (chain_whole _ _ _ Ch) as [Ts C1].
      assert (Lc : S (length ts) = c + length content).
      { change (S (length ts)) with (length (TTag shareable :: ts)). rewrite Ts, length_app, repeat_length. reflexivity. }
      rewrite Ts. replace (S f) with (c + (S f - c)) by lia. rewrite item_end_tags.
      apply (IH _ _ _ _ _ H). lia.
    + destruct (Nat.eqb tg sharedref).
      * destruct ts as [| [i | | | |] ts']; try discriminate.
        destruct (nth_error (table s) i); [| discriminate]. destruct (_ <? _); [| discriminate]. drop_cyclic.
        inversion H; subst. destruct f as [| f]; [simpl in L; lia | reflexivity].
      * destruct (read_item fuel ts (reserved (looked 1 s))) as [[[c r1] s1] | e] eqn:E; [| discriminate].
        inversion H; subst. simpl. apply (IH _ _ _ _ _ E). lia.
Qed.

(* cycles_ok_items checks k items one after the other with the check of one item. *)
Fixpoint cycles_ok_items (ck : list tok -> nat -> list nat -> option (list tok * nat * list nat)) (k : nat)
  (ts : list tok) (n : nat) (tb : list nat) : option (list tok * nat * list nat) :=
  match k with
  | 0 => Some (ts, n, tb)
  | S k' =>
    match ck ts n tb with
    | Some (r, n1, tb1) => cycles_ok_items ck k' r n1 tb1
    | None => None
    end
  end.

(* cycles_ok checks one item as read_item reads it, without the graph. n is the next id, tb the table and opn the
   ids of the items whose read has not ended. It gives the stream after the item, the next id and the table, or
   None. A node takes n, and n is in opn while the content of the node is checked. A tag 29 passes only if it
   names an id in opn: it closes a cycle. *)
Fixpoint cycles_ok (fuel : nat) (ts : list tok) (n : nat) (tb : list nat) (opn : list nat) {struct fuel}
  : option (list tok * nat * list nat) :=
  match fuel with
  | 0 => None
  | S f =>
    match ts with
    | [] => None
    | TUInt _ :: r => Some (r, S n, tb)
    | TLeaf _ :: r => Some (r, S n, tb)
    | TArr k :: r => cycles_ok_items (fun x m t => cycles_ok f x m t (n :: opn)) k r (S n) tb
    | TMap k :: r => cycles_ok_items (fun x m t => cycles_ok f x m t (n :: opn)) (k + k) r (S n) tb
    | TTag t :: r =>
      if Nat.eqb t shareable then
        let (c, content) := chain r 1 in
        match chain_target content c (mkstate n tb [] 0) with
        | ok target => cycles_ok f content n (tb ++ repeat target c) opn
        | fail _ => None
        end
      else if Nat.eqb t sharedref then
        match r with
        | TUInt i :: r' =>
          match nth_error tb i with
          | Some x => if x <? n then if existsb (Nat.eqb x) opn then Some (r', n, tb) else None else None
          | None => None
          end
        | _ => None
        end
      else cycles_ok f r (S n) tb (n :: opn)
    end
  end.

(* opn holds the ids below next s whose nodes are not complete, and no other id. *)
Definition open_nodes (s : state) (opn : list nat) : Prop :=
  (forall x, x < next s -> complete x (graph s) = false <-> In x opn) /\ (forall x, In x opn -> x < next s).

(* chain_target reads only the table and the next id. *)
Lemma chain_target_graph : forall content c s,
  chain_target content c s = chain_target content c (mkstate (next s) (table s) [] 0).
Proof. intros content c [n tb g st]. reflexivity. Qed.

Lemma existsb_In : forall x l, existsb (Nat.eqb x) l = true <-> In x l.
Proof.
  intros x l. rewrite existsb_exists. split.
  - intros [y [I E]]. apply Nat.eqb_eq in E. subst y. exact I.
  - intros I. exists x. split; [exact I | apply Nat.eqb_refl].
Qed.

Lemma complete_cons : forall x i nd g, complete x ((i, nd) :: g) = orb (Nat.eqb i x) (complete x g).
Proof. reflexivity. Qed.

Lemma complete_next : forall s, sound s -> complete (next s) (graph s) = false.
Proof.
  intros s Sd. unfold complete. destruct (existsb _ _) eqn:E; [| reflexivity].
  apply existsb_exists in E as [[i nd] [I Ei]]. simpl in Ei. apply Nat.eqb_eq in Ei. subst i.
  destruct (Sd _ _ I) as [L _]. lia.
Qed.

(* A node opens when the reader reserves its id. *)
Lemma open_nodes_rl : forall s opn, sound s -> open_nodes s opn -> open_nodes (reserved (looked 1 s)) (next s :: opn).
Proof.
  intros s opn Sd [Oc Ol]. split; cbn [next graph reserved looked].
  - intros x Lx. destruct (Nat.eq_dec x (next s)) as [-> | D].
    + rewrite (complete_next s Sd). split; [intros _; left; reflexivity | reflexivity].
    + rewrite (Oc x ltac:(lia)). simpl. split; [intros I; right; exact I | intros [E | I]; [lia | exact I]].
  - intros x [<- | I]; [lia | specialize (Ol x I); lia].
Qed.

(* A node closes when it enters the graph. *)
Lemma open_nodes_made : forall s s1 opn nd, open_nodes s opn -> open_nodes s1 (next s :: opn) ->
  open_nodes (made (next s) nd s1) opn.
Proof.
  intros s s1 opn nd [Oc Ol] [Oc1 Ol1].
  assert (L1 : next s < next s1) by (apply Ol1; left; reflexivity).
  split; cbn [next graph made].
  - intros x Lx. rewrite complete_cons. destruct (Nat.eqb (next s) x) eqn:E; simpl.
    + apply Nat.eqb_eq in E. subst x. split; [discriminate | intros I; specialize (Ol _ I); lia].
    + apply Nat.eqb_neq in E. rewrite (Oc1 x Lx). simpl. split; [intros [E1 | I]; [lia | exact I] | intros I; right; exact I].
  - intros x I. specialize (Ol x I). lia.
Qed.

Lemma items_cycles : forall f opn,
  (forall ts s, open_nodes s opn -> sound s ->
     match read_item f ts s with
     | ok (_, rest, s') => cycles_ok f ts (next s) (table s) opn = Some (rest, next s', table s') /\ open_nodes s' opn
     | fail _ => cycles_ok f ts (next s) (table s) opn = None
     end) ->
  forall k ts s, open_nodes s opn -> sound s ->
    match items (read_item f) k ts s with
    | ok (_, rest, s') =>
      cycles_ok_items (fun x m t => cycles_ok f x m t opn) k ts (next s) (table s) = Some (rest, next s', table s') /\
      open_nodes s' opn
    | fail _ => cycles_ok_items (fun x m t => cycles_ok f x m t opn) k ts (next s) (table s) = None
    end.
Proof.
  intros f opn Hr. induction k as [| k IH]; intros ts s O Sd; cbn [items cycles_ok_items].
  - split; [reflexivity | exact O].
  - pose proof (Hr ts s O Sd) as H1.
    destruct (read_item f ts s) as [[[id1 r1] s1] | e] eqn:E1.
    + destruct H1 as [C1 O1]. rewrite C1.
      pose proof (IH r1 s1 O1 (proj1 (read_item_sound _ _ _ _ _ _ E1 Sd))) as H2.
      destruct (items (read_item f) k r1 s1) as [[[ids r2] s2] | e]; exact H2.
    + rewrite H1. reflexivity.
Qed.

(* read_item and cycles_ok agree: the read succeeds exactly where cycles_ok passes, and both give the same stream,
   the same next id and the same table. *)
Lemma read_item_cycles : forall fuel ts s opn, open_nodes s opn -> sound s ->
  match read_item fuel ts s with
  | ok (_, rest, s') => cycles_ok fuel ts (next s) (table s) opn = Some (rest, next s', table s') /\ open_nodes s' opn
  | fail _ => cycles_ok fuel ts (next s) (table s) opn = None
  end.
Proof.
  induction fuel as [| f IH]; intros ts s opn O Sd; [reflexivity |].
  pose proof (open_nodes_rl s opn Sd O) as O1.
  pose proof (sound_rl s Sd) as S1.
  destruct ts as [| [n | n | k | k | t] r]; cbn [read_item cycles_ok]; [reflexivity | | | | |].
  - split; [reflexivity | exact (open_nodes_made s _ opn _ O O1)].
  - split; [reflexivity | exact (open_nodes_made s _ opn _ O O1)].
  - pose proof (items_cycles f (next s :: opn) (fun x s0 => IH x s0 (next s :: opn)) k r _ O1 S1) as Hi.
    destruct (items (read_item f) k r (reserved (looked 1 s))) as [[[ids r1] s2] | e]; [| exact Hi].
    destruct Hi as [C O2]. split; [exact C | exact (open_nodes_made s _ opn _ O O2)].
  - pose proof (items_cycles f (next s :: opn) (fun x s0 => IH x s0 (next s :: opn)) (k + k) r _ O1 S1) as Hi.
    destruct (items (read_item f) (k + k) r (reserved (looked 1 s))) as [[[ids r1] s2] | e]; [| exact Hi].
    destruct Hi as [C O2]. split; [exact C | exact (open_nodes_made s _ opn _ O O2)].
  - destruct (Nat.eqb t shareable).
    + destruct (chain r 1) as [c content].
      rewrite (chain_target_graph content c s).
      destruct (chain_target content c (mkstate (next s) (table s) [] 0)) as [target | e]; [| reflexivity].
      pose proof (IH content (marked_as target c (looked c s)) opn O Sd) as Hc.
      destruct (read_item f content (marked_as target c (looked c s))) as [[[id r1] s2] | e]; exact Hc.
    + destruct (Nat.eqb t sharedref).
      * destruct r as [| [i | | | |] r']; try reflexivity.
        destruct (nth_error (table s) i) as [x |]; [| reflexivity].
        destruct (x <? next s) eqn:Lx; [| reflexivity].
        apply Nat.ltb_lt in Lx. destruct O as [Oc Ol]. pose proof (Oc x Lx) as Ox.
        destruct (complete x (graph s)) eqn:C; destruct (existsb (Nat.eqb x) opn) eqn:X.
        -- apply existsb_In in X. apply Ox in X. discriminate.
        -- reflexivity.
        -- split; [reflexivity | exact (conj Oc Ol)].
        -- pose proof (proj2 (existsb_In x opn) (proj1 Ox eq_refl)) as I. congruence.
      * pose proof (IH r (reserved (looked 1 s)) (next s :: opn) O1 S1) as Hc.
        destruct (read_item f r (reserved (looked 1 s))) as [[[id r1] s2] | e]; [| exact Hc].
        destruct Hc as [C O2]. split; [exact C | exact (open_nodes_made s _ opn _ O O2)].
Qed.

Lemma open_nodes_start : open_nodes start [].
Proof. split; [intros x L; simpl in L; lia | intros x []]. Qed.

Lemma sound_start : sound start.
Proof. intros i n []. Qed.

(* T6, one direction: a stream that read reads is one well-formed item, and each of its tags 29 closes a cycle. *)
Theorem read_stream : forall ts root s, read ts = ok (root, s) ->
  well_formed ts /\ cycles_ok (S (length ts)) ts 0 [] [] <> None.
Proof.
  intros ts root s H. unfold read in H.
  pose proof (read_item_cycles (S (length ts)) ts start [] open_nodes_start sound_start) as Hc.
  destruct (read_item (S (length ts)) ts start) as [[[r rest] s'] | e] eqn:E; [| discriminate].
  destruct rest; [| discriminate].
  split.
  - unfold well_formed. eapply read_item_ends; [exact E | lia].
  - destruct Hc as [C _]. cbn [next table start] in C. rewrite C. discriminate.
Qed.

(* T6, the other direction: read reads each well-formed stream whose tags 29 close cycles. *)
Theorem stream_read : forall ts, well_formed ts -> cycles_ok (S (length ts)) ts 0 [] [] <> None ->
  exists root s, read ts = ok (root, s).
Proof.
  intros ts W Cn.
  pose proof (read_item_cycles (S (length ts)) ts start [] open_nodes_start sound_start) as Hc.
  unfold read.
  destruct (read_item (S (length ts)) ts start) as [[[id rest] s'] | e] eqn:E; [| exfalso; exact (Cn Hc)].
  pose proof (read_item_ends _ _ _ _ _ _ E (S (length ts)) ltac:(lia)) as Er.
  unfold well_formed in W. rewrite W in Er. injection Er as <-.
  exists id, s'. reflexivity.
Qed.

(* T6: read succeeds if and only if the stream is one well-formed item and each of its tags 29 closes a cycle. *)
Theorem read_ok_iff : forall ts, (exists root s, read ts = ok (root, s)) <->
  well_formed ts /\ cycles_ok (S (length ts)) ts 0 [] [] <> None.
Proof.
  intros ts. split.
  - intros [root [s H]]. exact (read_stream ts root s H).
  - intros [W C]. exact (stream_read ts W C).
Qed.

(* Cases with their result, computed by the model. *)

(* The specification: 28([29(0)]), an array that holds itself, read to depth 3. *)
Example expand_cyclic_array : expand 3 [TTag 28; TArr 1; TTag 29; TUInt 0] = TA [TA [TA [Cut]]].
Proof. reflexivity. Qed.

(* The specification: [28([]), 29(0), []]. read refuses it (spec_shared_array); expand duplicates the value. *)
Example expand_shared_array :
  expand 2 [TArr 3; TTag 28; TArr 0; TTag 29; TUInt 0; TArr 0] = TA [TA []; TA []; TA []].
Proof. reflexivity. Qed.

(* A mark of a reference: [28(1), 28(29(0)), 29(1)]. read refuses it (mark_of_reference_to_complete); expand
   duplicates the value. *)
Example expand_mark_of_reference :
  expand 2 [TArr 3; TTag 28; TUInt 1; TTag 28; TTag 29; TUInt 0; TTag 29; TUInt 1] = TA [TU 1; TU 1; TU 1].
Proof. reflexivity. Qed.

(* cycles_ok refuses a reference to a mark in the chain directly before it: 28(28(29(0))). *)
Example cycles_chain_of_itself : cycles_ok 5 [TTag 28; TTag 28; TTag 29; TUInt 0] 0 [] [] = None.
Proof. reflexivity. Qed.

(* cycles_ok refuses a reference before its mark: [29(0), 28(1)]. *)
Example cycles_before_mark : cycles_ok 6 [TArr 2; TTag 29; TUInt 0; TTag 28; TUInt 1] 0 [] [] = None.
Proof. reflexivity. Qed.

(* cycles_ok refuses a reference to a complete value: [28([]), 29(0), []]. *)
Example cycles_shared_array : cycles_ok 7 [TArr 3; TTag 28; TArr 0; TTag 29; TUInt 0; TArr 0] 0 [] [] = None.
Proof. reflexivity. Qed.

(* cycles_ok passes a reference that closes a cycle: 28([29(0)]). *)
Example cycles_cyclic_array : cycles_ok 5 [TTag 28; TArr 1; TTag 29; TUInt 0] 0 [] [] = Some ([], 1, [0]).
Proof. reflexivity. Qed.
