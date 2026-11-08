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

(* The stream after a chain of tags 28. *)
Definition strip (ts : list tok) : list tok := snd (chain ts 0).
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
Lemma chain_snd : forall ts c, snd (chain ts c) = snd (chain ts 0).
Proof.
  induction ts as [| t ts IH]; intros c; simpl; [reflexivity |].
  destruct t as [| | | | tg]; try reflexivity.
  destruct (Nat.eqb tg shareable); [rewrite (IH (S c)), (IH 1); reflexivity | reflexivity].
Qed.

Lemma strip_chain : forall ts c c' content, chain ts c = (c', content) -> strip ts = content.
Proof. intros ts c c' content H. unfold strip. rewrite <- (chain_snd ts c), H. reflexivity. Qed.

Lemma strip_shareable : forall r, strip (TTag shareable :: r) = strip r.
Proof. intros r. unfold strip. simpl. apply chain_snd. Qed.

Lemma strip_simple : forall ts, (forall r, ts <> TTag shareable :: r) -> strip ts = ts.
Proof.
  intros [| [n | n | n | n | t] r] H; unfold strip; simpl; try reflexivity.
  destruct (Nat.eqb t shareable) eqn:M; [| reflexivity].
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
        destruct (nth_error (table s) i); [| discriminate]. destruct (_ <? _); [| discriminate].
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
        destruct (nth_error (table s) i); [| discriminate]. destruct (_ <? _); [| discriminate].
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
        destruct (nth_error (table s) i); [| discriminate]. destruct (_ <? _); [| discriminate].
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
        destruct (nth_error (table s) i); [| discriminate]. destruct (_ <? _); [| discriminate].
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
        destruct (x <? next s); [| discriminate].
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

(* Each tag 29 has an unsigned integer as its index. The index names a tag 28 before the tag 29 that is not in the
   chain of tags 28 directly before the tag 29. count: the tags 28 before; run: the tags 28 directly before. *)
Fixpoint refs_ok (ts : list tok) (count run : nat) : Prop :=
  match ts with
  | [] => True
  | TTag t :: r =>
    if Nat.eqb t shareable then refs_ok r (S count) (S run)
    else if Nat.eqb t sharedref then
      match r with
      | TUInt i :: r' => i + run < count /\ refs_ok r' count 0
      | _ => False
      end
    else refs_ok r count 0
  | _ :: r => refs_ok r count 0
  end.

Lemma refs_ref : forall i r c run, refs_ok (TTag sharedref :: TUInt i :: r) c run = (i + run < c /\ refs_ok r c 0).
Proof. reflexivity. Qed.

Lemma refs_repeat : forall k x c run,
  refs_ok (repeat (TTag shareable) k ++ x) c run = refs_ok x (c + k) (run + k).
Proof.
  induction k as [| k IH]; intros x c run.
  - rewrite !Nat.add_0_r. reflexivity.
  - change (refs_ok (repeat (TTag shareable) k ++ x) (S c) (S run) = refs_ok x (c + S k) (run + S k)).
    rewrite IH. replace (S c + k) with (c + S k) by lia. replace (S run + k) with (run + S k) by lia. reflexivity.
Qed.

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
        destruct (nth_error (table s) i); [| discriminate]. destruct (_ <? _); [| discriminate].
        inversion H; subst. destruct f as [| f]; [simpl in L; lia | reflexivity].
      * destruct (read_item fuel ts (reserved (looked 1 s))) as [[[c r1] s1] | e] eqn:E; [| discriminate].
        inversion H; subst. simpl. apply (IH _ _ _ _ _ E). lia.
Qed.

(* A successful read meets refs_ok. A read that starts after a chain of tags 28 reads an item that makes a node. *)
Lemma items_refs : forall fuel,
  (forall ts s id rest s' r, read_item fuel ts s = ok (id, rest, s') -> (r = 0 \/ simple ts) ->
     refs_ok rest (length (table s')) 0 -> refs_ok ts (length (table s)) r) ->
  forall n x a ids rest b, items (read_item fuel) n x a = ok (ids, rest, b) ->
    refs_ok rest (length (table b)) 0 -> refs_ok x (length (table a)) 0.
Proof.
  intros fuel Hr. induction n as [| n IH]; intros x a ids rest b H R; simpl in H.
  - inversion H; subst. exact R.
  - destruct (read_item fuel x a) as [[[id1 rest1] s1] | e] eqn:E1; [| discriminate].
    destruct (items (read_item fuel) n rest1 s1) as [[[ids2 rest2] s2] | e] eqn:E2; [| discriminate].
    inversion H; subst.
    apply (Hr _ _ _ _ _ 0 E1 (or_introl eq_refl)). eapply IH; [exact E2 | exact R].
Qed.

Lemma read_item_refs : forall fuel ts s id rest s' r, read_item fuel ts s = ok (id, rest, s') ->
  (r = 0 \/ simple ts) -> refs_ok rest (length (table s')) 0 -> refs_ok ts (length (table s)) r.
Proof.
  induction fuel as [| fuel IH]; intros ts s id rest s' r H Hr R; [discriminate |].
  simpl in H. destruct ts as [| [n | n | k | k | tg] ts]; try discriminate.
  - inversion H; subst. exact R.
  - inversion H; subst. exact R.
  - destruct (items (read_item fuel) k ts (reserved (looked 1 s))) as [[[ids r1] s1] | e] eqn:E; [| discriminate].
    inversion H; subst. exact (items_refs _ IH _ _ _ _ _ _ E R).
  - destruct (items (read_item fuel) (k + k) ts (reserved (looked 1 s))) as [[[ids r1] s1] | e] eqn:E; [| discriminate].
    inversion H; subst. exact (items_refs _ IH _ _ _ _ _ _ E R).
  - destruct (Nat.eqb tg shareable) eqn:M.
    + apply Nat.eqb_eq in M. subst tg.
      assert (r = 0) as -> by (destruct Hr as [-> | [N _]]; [reflexivity | exfalso; apply N; reflexivity]).
      destruct (chain ts 1) as [c content] eqn:Ch.
      destruct (chain_target content c s) as [target | e] eqn:CT; [| discriminate].
      destruct (chain_whole _ _ _ Ch) as [Ts C1].
      rewrite Ts, refs_repeat.
      assert (Lm : length (table (marked_as target c (looked c s))) = length (table s) + c).
      { simpl. rewrite length_app, repeat_length. reflexivity. }
      destruct (chain_target_cases _ _ _ _ CT) as [[i0 [r0 [Ec Ei]]] | [Et Hc]].
      * subst content. destruct fuel as [| fuel]; [discriminate |].
        apply sharedref_names_entry in H as [_ [Es Er]]. subst s' rest.
        rewrite refs_ref. split.
        -- assert (Li : i0 < length (table s)) by (apply nth_error_Some; rewrite Ei; discriminate). lia.
        -- change (refs_ok r0 (length (table (marked_as target c (looked c s)))) 0) in R. rewrite Lm in R. exact R.
      * rewrite <- Lm. apply (IH _ _ _ _ _ (0 + c) H); [| exact R].
        right. apply simple_of; [eapply chain_content_head; exact Ch | exact Hc].
    + destruct (Nat.eqb tg sharedref) eqn:M29.
      * apply Nat.eqb_eq in M29. subst tg.
        assert (r = 0) as -> by (destruct Hr as [-> | [_ N]]; [reflexivity | exfalso; apply N; reflexivity]).
        destruct ts as [| [i | | | |] ts']; try discriminate.
        destruct (nth_error (table s) i) as [x |] eqn:Ex; [| discriminate]. destruct (_ <? _); [| discriminate].
        inversion H; subst. rewrite refs_ref. split; [| exact R].
        assert (Li : i < length (table s)) by (apply nth_error_Some; rewrite Ex; discriminate). lia.
      * destruct (read_item fuel ts (reserved (looked 1 s))) as [[[c r1] s1] | e] eqn:E; [| discriminate].
        inversion H; subst. simpl. rewrite M, M29.
        exact (IH _ _ _ _ _ 0 E (or_introl eq_refl) R).
Qed.

(* T6, one direction: a stream that read reads is one well-formed item and meets refs_ok. *)
Theorem read_stream : forall ts root s, read ts = ok (root, s) -> well_formed ts /\ refs_ok ts 0 0.
Proof.
  intros ts root s H. unfold read in H.
  destruct (read_item (S (length ts)) ts start) as [[[r rest] s'] | e] eqn:E; [| discriminate].
  destruct rest; [| discriminate].
  split.
  - unfold well_formed. eapply read_item_ends; [exact E | lia].
  - exact (read_item_refs _ _ _ _ _ _ 0 E (or_introl eq_refl) I).
Qed.

(* The other direction: each entry of the table is a reserved id. After a chain of tags 28 whose item makes a node,
   the entries may be the next id, which the item reserves first. *)
Definition below (s : state) : Prop := forall j x, nth_error (table s) j = Some x -> x < next s.
Definition below_eq (s : state) : Prop := forall j x, nth_error (table s) j = Some x -> x <= next s.

Lemma below_rl : forall s, below_eq s -> below (reserved (looked 1 s)).
Proof. intros s B j x Ej. specialize (B j x Ej). simpl. lia. Qed.

Lemma nth_error_app_repeat : forall (l : list nat) a c j y,
  nth_error (l ++ repeat a c) j = Some y -> nth_error l j = Some y \/ y = a.
Proof.
  intros l a c j y E.
  destruct (Nat.lt_ge_cases j (length l)) as [L | L].
  - left. rewrite nth_error_app1 in E by exact L. exact E.
  - right. rewrite nth_error_app2 in E by exact L. apply nth_error_In in E. apply repeat_spec in E. exact E.
Qed.

Lemma read_item_chain_eq : forall f r s c content, chain r 1 = (c, content) ->
  read_item (S f) (TTag shareable :: r) s =
  match chain_target content c s with
  | fail e => fail e
  | ok target => read_item f content (marked_as target c (looked c s))
  end.
Proof. intros f r s c content Ch. simpl. rewrite Ch. reflexivity. Qed.

Lemma read_item_ref_ok : forall f i r s x, nth_error (table s) i = Some x -> x < next s ->
  read_item (S f) (TTag sharedref :: TUInt i :: r) s = ok (x, r, looked 2 s).
Proof. intros f i r s x E L. simpl. rewrite E. apply Nat.ltb_lt in L. rewrite L. reflexivity. Qed.

Lemma chain_target_ref : forall i r c s x, nth_error (table s) i = Some x -> x < next s ->
  chain_target (TTag sharedref :: TUInt i :: r) c s = ok x.
Proof. intros i r c s x E L. unfold chain_target. simpl. rewrite E. apply Nat.ltb_lt in L. rewrite L. reflexivity. Qed.

Lemma chain_target_simple : forall content c s, simple content -> chain_target content c s = ok (next s).
Proof.
  intros [| [n | n | n | n | t] r] c s Hs; try reflexivity.
  destruct Hs as [_ N]. apply Nat.eqb_neq in N. unfold chain_target. simpl. rewrite N. reflexivity.
Qed.

Definition complete_at (fuel : nat) : Prop :=
  forall ts s rest r, length ts < fuel -> item_end (S (length ts)) ts = Some rest ->
    refs_ok ts (length (table s)) r -> below s \/ (below_eq s /\ simple ts) ->
    exists id s', read_item fuel ts s = ok (id, rest, s') /\ refs_ok rest (length (table s')) 0 /\ below s'.

Lemma items_complete : forall fuel F, complete_at fuel ->
  forall n x a rest, length x < fuel -> length x < F -> skips (item_end F) n x = Some rest ->
    refs_ok x (length (table a)) 0 -> below a ->
    exists ids b, items (read_item fuel) n x a = ok (ids, rest, b) /\ refs_ok rest (length (table b)) 0 /\ below b.
Proof.
  intros fuel F Hc. induction n as [| n IH]; intros x a rest L LF H R B; simpl in H.
  - inversion H; subst. exists [], a. split; [reflexivity | split; assumption].
  - destruct (item_end F x) as [r1 |] eqn:E1; [| discriminate].
    assert (E1' : item_end (S (length x)) x = Some r1) by (eapply item_end_enough; [exact E1 | lia]).
    destruct (Hc x a r1 0 L E1' R (or_introl B)) as [id [b1 [Hr1 [R1 B1]]]].
    pose proof (item_end_shorter _ _ _ E1) as L1.
    destruct (IH r1 b1 rest ltac:(lia) ltac:(lia) H R1 B1) as [ids [b [Hi [Rb Bb]]]].
    exists (id :: ids), b. split; [simpl; rewrite Hr1, Hi; reflexivity | split; assumption].
Qed.

Lemma read_item_complete : forall fuel, complete_at fuel.
Proof.
  induction fuel as [| f IH]; intros ts s rest r L W R B; [simpl in L; lia |].
  assert (Be : below_eq s) by (destruct B as [B | [B _]]; [intros j x E0; specialize (B j x E0); lia | exact B]).
  destruct ts as [| [n | n | n | n | t] r0]; try discriminate W.
  - change (Some r0 = Some rest) in W. injection W as <-.
    exists (next s), (made (next s) (NUInt n) (reserved (looked 1 s))).
    split; [reflexivity | split; [exact R | intros j x Ej; specialize (Be j x Ej); simpl; lia]].
  - change (Some r0 = Some rest) in W. injection W as <-.
    exists (next s), (made (next s) (NLeaf n) (reserved (looked 1 s))).
    split; [reflexivity | split; [exact R | intros j x Ej; specialize (Be j x Ej); simpl; lia]].
  - change (skips (item_end (S (length r0))) n r0 = Some rest) in W.
    simpl in L.
    destruct (items_complete f (S (length r0)) IH n r0 (reserved (looked 1 s)) rest ltac:(lia) ltac:(lia) W R (below_rl _ Be))
      as [ids [b [Hi [Rb Bb]]]].
    exists (next s), (made (next s) (NArr ids) b). split; [simpl; rewrite Hi; reflexivity | split; [exact Rb | exact Bb]].
  - change (skips (item_end (S (length r0))) (n + n) r0 = Some rest) in W.
    simpl in L.
    destruct (items_complete f (S (length r0)) IH (n + n) r0 (reserved (looked 1 s)) rest ltac:(lia) ltac:(lia) W R (below_rl _ Be))
      as [ids [b [Hi [Rb Bb]]]].
    exists (next s), (made (next s) (NMap (pairs ids)) b). split; [simpl; rewrite Hi; reflexivity | split; [exact Rb | exact Bb]].
  - change (item_end (S (length r0)) r0 = Some rest) in W.
    destruct (Nat.eqb t shareable) eqn:M28.
    + (* A chain of tags 28. *)
      apply Nat.eqb_eq in M28. subst t.
      destruct B as [B | [_ [N _]]]; [| exfalso; apply N; reflexivity].
      destruct (chain r0 1) as [c content] eqn:Ch.
      destruct (chain_whole _ _ _ Ch) as [Ts C1].
      destruct (chain_repeat _ _ _ _ Ch) as [Rt _].
      assert (Lc : length r0 = c - 1 + length content) by (rewrite Rt, length_app, repeat_length; reflexivity).
      rewrite Ts, refs_repeat in R.
      assert (Wc : item_end (S (length content)) content = Some rest).
      { rewrite Rt, length_app, repeat_length in W.
        replace (S (c - 1 + length content)) with (c - 1 + S (length content)) in W by lia.
        rewrite item_end_tags in W. exact W. }
      assert (Lm : length (table (marked_as (next s) c (looked c s))) = length (table s) + c).
      { simpl. rewrite length_app, repeat_length. reflexivity. }
      assert (K : (exists r1, content = TTag sharedref :: r1) \/ simple content).
      { destruct content as [| [n1 | n1 | n1 | n1 | t1] r1]; try (right; exact I).
        destruct (Nat.eqb t1 sharedref) eqn:M29.
        - left. apply Nat.eqb_eq in M29. subst t1. eexists. reflexivity.
        - right. split; [intros E0; subst t1; eapply chain_content_head; [exact Ch | reflexivity] | apply Nat.eqb_neq; exact M29]. }
      destruct K as [[r1 ->] | Hs].
      * (* The chain marks a reference to an earlier mark. *)
        destruct r1 as [| [i | | | |] r2]; try (exfalso; exact R).
        rewrite refs_ref in R. destruct R as [Li R2].
        change (Some r2 = Some rest) in Wc. injection Wc as <-.
        destruct (nth_error (table s) i) as [x |] eqn:Ex; [| apply nth_error_None in Ex; lia].
        exists x, (looked 2 (marked_as x c (looked c s))). split; [| split].
        -- rewrite (read_item_chain_eq _ _ _ _ _ Ch), (chain_target_ref _ _ _ _ _ Ex (B _ _ Ex)).
           destruct f as [| f]; [simpl in L; lia |].
           apply read_item_ref_ok; [exact (nth_error_app_prefix _ _ _ _ Ex) | exact (B _ _ Ex)].
        -- change (refs_ok r2 (length (table s ++ repeat x c)) 0). rewrite length_app, repeat_length. exact R2.
        -- intros j y Ej. destruct (nth_error_app_repeat _ _ _ _ _ Ej) as [Ej' | ->]; [exact (B _ _ Ej') | exact (B _ _ Ex)].
      * (* The chain marks an item that makes a node. *)
        rewrite <- Lm in R.
        destruct (IH content (marked_as (next s) c (looked c s)) rest (r + c) ltac:(simpl in L; lia) Wc R) as [id [s' [Hr [Rs Bs]]]].
        { right. split; [| exact Hs]. intros j y Ej.
          destruct (nth_error_app_repeat _ _ _ _ _ Ej) as [Ej' | ->]; [specialize (B _ _ Ej'); simpl; lia | simpl; lia]. }
        exists id, s'. split; [| split; assumption].
        rewrite (read_item_chain_eq _ _ _ _ _ Ch), (chain_target_simple _ _ _ Hs). exact Hr.
    + destruct (Nat.eqb t sharedref) eqn:M29.
      * (* A tag 29. *)
        apply Nat.eqb_eq in M29. subst t.
        destruct B as [B | [_ [_ N]]]; [| exfalso; apply N; reflexivity].
        destruct r0 as [| [i | | | |] r2]; try (exfalso; exact R).
        rewrite refs_ref in R. destruct R as [Li R2].
        change (Some r2 = Some rest) in W. injection W as <-.
        destruct (nth_error (table s) i) as [x |] eqn:Ex; [| apply nth_error_None in Ex; lia].
        exists x, (looked 2 s). split; [apply read_item_ref_ok; [exact Ex | exact (B _ _ Ex)] | split; [exact R2 | exact B]].
      * (* Another tag. *)
        simpl in R. rewrite M28, M29 in R.
        destruct (IH r0 (reserved (looked 1 s)) rest 0 ltac:(simpl in L; lia) W R (or_introl (below_rl _ Be)))
          as [id [s1 [Hr [Rs Bs]]]].
        exists (next s), (made (next s) (NTag t id) s1).
        split; [simpl; rewrite M28, M29, Hr; reflexivity | split; [exact Rs | exact Bs]].
Qed.

(* T6, the other direction: read reads each well-formed stream that meets refs_ok. *)
Theorem stream_read : forall ts, well_formed ts -> refs_ok ts 0 0 -> exists root s, read ts = ok (root, s).
Proof.
  intros ts W R.
  destruct (read_item_complete (S (length ts)) ts start [] 0 ltac:(lia) W R) as [id [s' [H _]]].
  { left. intros [| j] x Ej; discriminate Ej. }
  exists id, s'. unfold read. rewrite H. reflexivity.
Qed.

(* T6: read succeeds if and only if the stream is one well-formed item and meets refs_ok. *)
Theorem read_ok_iff : forall ts, (exists root s, read ts = ok (root, s)) <-> well_formed ts /\ refs_ok ts 0 0.
Proof.
  intros ts. split.
  - intros [root [s H]]. exact (read_stream ts root s H).
  - intros [W R]. exact (stream_read ts W R).
Qed.

(* Cases with their result, computed by the model. *)

(* The specification: 28([29(0)]), an array that holds itself, read to depth 3. *)
Example expand_cyclic_array : expand 3 [TTag 28; TArr 1; TTag 29; TUInt 0] = TA [TA [TA [Cut]]].
Proof. reflexivity. Qed.

(* The specification: [28([]), 29(0), []]. *)
Example expand_shared_array :
  expand 2 [TArr 3; TTag 28; TArr 0; TTag 29; TUInt 0; TArr 0] = TA [TA []; TA []; TA []].
Proof. reflexivity. Qed.

(* A mark of a reference: [28(1), 28(29(0)), 29(1)]. *)
Example expand_mark_of_reference :
  expand 2 [TArr 3; TTag 28; TUInt 1; TTag 28; TTag 29; TUInt 0; TTag 29; TUInt 1] = TA [TU 1; TU 1; TU 1].
Proof. reflexivity. Qed.

(* refs_ok refuses a reference to a mark in the chain directly before it: 28(28(29(0))). *)
Example refs_chain_of_itself : ~ refs_ok [TTag 28; TTag 28; TTag 29; TUInt 0] 0 0.
Proof. simpl. lia. Qed.

(* refs_ok refuses a reference before its mark: [29(0), 28(1)]. *)
Example refs_before_mark : ~ refs_ok [TArr 2; TTag 29; TUInt 0; TTag 28; TUInt 1] 0 0.
Proof. simpl. lia. Qed.
